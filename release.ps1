#!/usr/bin/env pwsh
[CmdletBinding()]
param(
    # Build and verify local assets only; do not contact GitHub.
    [switch]$BuildOnly,
    # Create and expose a prerelease instead of leaving a draft.
    [switch]$Candidate,
    # Promote an existing draft/prerelease without rebuilding its assets.
    [switch]$Publish
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot
$Repo = 'vault-boy-13/usb-pd-ufp'
$SourcePath = Join-Path $Root 'source/main.c'
$ArchivePath = $null
$ChecksumsPath = Join-Path $Root 'SHA256SUMS.txt'
$savedGitPrompt = [Environment]::GetEnvironmentVariable('GIT_TERMINAL_PROMPT', 'Process')
$env:GIT_TERMINAL_PROMPT = '0'
Push-Location -LiteralPath $Root

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments, [string]$Action)
    $output = & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Action failed (exit code $LASTEXITCODE)."
    }
    return $output
}

function Get-ProjectVersion {
    $source = Get-Content -LiteralPath $SourcePath -Raw -Encoding UTF8
    $match = [regex]::Match($source, '(?m)^#define VERSION "(\d+\.\d+\.\d+)"\r?$')
    if (-not $match.Success) { throw 'source/main.c must define VERSION as MAJOR.MINOR.PATCH.' }
    return $match.Groups[1].Value
}

function Invoke-RepoGit {
    param([string[]]$Arguments, [string]$Action)
    # Use gh credentials for HTTPS without changing the user's Git config.
    Invoke-Checked 'git' (@('-C', $Root, '-c', 'credential.helper=',
        '-c', 'credential.https://github.com.helper=!gh auth git-credential') + $Arguments) $Action
}

function Build-ReleaseAssets {
    param([string]$Version)

    $devkitProWin = 'C:\devkitPro'
    $bash = Join-Path $devkitProWin 'msys2/usr/bin/bash.exe'
    if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) {
        throw "MSYS2 bash not found: $bash. Set up devkitPro or adjust release.ps1."
    }

    $tempWin = Join-Path $Root '.release-tmp'
    if (Test-Path -LiteralPath $tempWin) {
        throw "Temporary build directory already exists; inspect or remove it first: $tempWin"
    }
    if ($Root -notmatch '^[A-Za-z]:\\') { throw 'Build path must be on a Windows drive letter.' }
    New-Item -ItemType Directory -Path $tempWin | Out-Null
    $drive = $Root.Substring(0, 1).ToLowerInvariant()
    $rootUnix = '/' + $drive + $Root.Substring(2).Replace('\', '/')
    $tempUnix = $rootUnix + '/.release-tmp'

    $savedEnvironment = @{}
    foreach ($name in @('DEVKITPRO', 'DEVKITA64', 'TEMP', 'TMP', 'TMPDIR')) {
        $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:DEVKITPRO = '/c/devkitPro'
    $env:DEVKITA64 = '/c/devkitPro/devkitA64'
    $env:TEMP = $tempUnix
    $env:TMP = $tempUnix
    $env:TMPDIR = $tempUnix

    Push-Location $Root
    try {
        & $bash -c 'export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:/usr/bin:$PATH"; make -B --no-print-directory all' |
            ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
    }
    finally {
        Pop-Location
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
        }
        # Only remove the known build-temp directory within this project.
        $resolvedTemp = [IO.Path]::GetFullPath($tempWin)
        if ($resolvedTemp -cne [IO.Path]::GetFullPath((Join-Path $Root '.release-tmp'))) {
            throw 'Unexpected temporary build path; refusing cleanup.'
        }
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }

    $archive = Test-ReleaseAssets $Version
    return $archive
}

function Test-ReleaseAssets {
    param([string]$Version)
    $archive = Join-Path $Root "usb-pd-ufp-$Version.zip"
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf) -or
        -not (Test-Path -LiteralPath $ChecksumsPath -PathType Leaf)) {
        throw 'Packager did not create the expected archive and SHA256SUMS.txt.'
    }

    $expectedHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $sumLines = @(Get-Content -LiteralPath $ChecksumsPath | Where-Object { $_.Trim() })
    $archiveName = [regex]::Escape((Split-Path $archive -Leaf))
    if ($sumLines.Count -ne 1 -or
        $sumLines[0] -notmatch ('^([0-9a-fA-F]{64})\s+\*?' + $archiveName + '$') -or
        $Matches[1].ToLowerInvariant() -cne $expectedHash) {
        throw 'SHA256SUMS.txt does not match the release archive.'
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName })
        $required = @(
            'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES', 'config/usb-pd-ufp/config.ini',
            'atmosphere/contents/42000000000000C1/exefs.nsp',
            'atmosphere/contents/42000000000000C1/flags/boot2.flag',
            'atmosphere/contents/42000000000000C1/toolbox.json'
        )
        if ($entries.Count -ne $required.Count -or
            @(Compare-Object -ReferenceObject $required -DifferenceObject $entries -CaseSensitive).Count -gt 0) {
            throw 'Release archive contents do not match the required file list.'
        }
    }
    finally {
        $zip.Dispose()
    }

    return $archive
}

function Get-Release {
    param([string]$Tag)
    $output = Invoke-Checked 'gh' @('release', 'view', $Tag, '--repo', $Repo,
        '--json', 'tagName,isDraft,isPrerelease,assets') 'Reading GitHub Release'
    return (($output -join "`n") | ConvertFrom-Json)
}

function Assert-Release {
    param($Release, [string]$Tag)
    $actual = @($Release.assets | ForEach-Object { [string]$_.name } | Sort-Object)
    $expected = @((Split-Path $ArchivePath -Leaf), 'SHA256SUMS.txt') | Sort-Object
    if ($Release.tagName -cne $Tag -or
        @(Compare-Object $expected $actual -CaseSensitive).Count -gt 0) {
        throw 'GitHub release tag or asset list does not match the local package.'
    }
}

function Compare-RemoteAssets {
    param([string]$Tag)
    $verifyDir = Join-Path ([IO.Path]::GetTempPath()) ('usb-pd-ufp-verify-' + [Guid]::NewGuid().ToString())
    $null = New-Item -ItemType Directory -Path $verifyDir
    $names = @((Split-Path $ArchivePath -Leaf), 'SHA256SUMS.txt')
    try {
        $null = Invoke-Checked 'gh' @('release', 'download', $Tag, '--repo', $Repo,
            '--dir', $verifyDir, '--pattern', $names[0], '--pattern', $names[1]) 'Downloading release assets for verification'
        foreach ($name in $names) {
            $downloaded = (Get-FileHash -LiteralPath (Join-Path $verifyDir $name) -Algorithm SHA256).Hash
            $local = (Get-FileHash -LiteralPath (Join-Path $Root $name) -Algorithm SHA256).Hash
            if ($downloaded -cne $local) { throw "GitHub release asset differs from local package: $name" }
        }
    }
    finally {
        foreach ($name in $names) {
            $path = Join-Path $verifyDir $name
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
        }
        Remove-Item -LiteralPath $verifyDir
    }
}

try {
    if (($BuildOnly -and ($Publish -or $Candidate)) -or ($Candidate -and $Publish)) {
        throw 'Use only one of -BuildOnly, -Candidate or -Publish.'
    }

    $version = Get-ProjectVersion
    $tag = "v$version"
    $ArchivePath = Join-Path $Root "usb-pd-ufp-$version.zip"

    if (-not $BuildOnly) {
        foreach ($command in @('git', 'gh')) {
            if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
                throw "Required command not found: $command"
            }
        }

        $null = Invoke-Checked 'gh' @('auth', 'status', '--hostname', 'github.com') 'GitHub authentication check'
        $remote = [string](Invoke-RepoGit @('remote', 'get-url', 'origin') 'Reading origin URL')
        if ($remote.Trim() -notmatch '^(?i:https://github\.com/|git@github\.com:|ssh://git@github\.com/)vault-boy-13/usb-pd-ufp(?:\.git)?$') {
            throw "origin must point to https://github.com/$Repo or git@github.com:$Repo.git."
        }

        $branch = [string](Invoke-RepoGit @('branch', '--show-current') 'Reading current branch')
        if ([string]::IsNullOrWhiteSpace($branch)) { throw 'Checkout is detached; switch to the release branch first.' }
        $dirty = @(Invoke-RepoGit @('status', '--porcelain', '--untracked-files=all') 'Inspecting Git working tree')
        if ($dirty.Count -gt 0) { throw 'Git working tree is not clean. Commit and push the release changes first.' }

        $head = [string](Invoke-RepoGit @('rev-parse', 'HEAD') 'Reading HEAD')
        # Query origin directly; avoid @{u}, which Windows wrappers may alter.
        $remoteBranch = @(Invoke-RepoGit @('ls-remote', '--heads', 'origin', "refs/heads/$($branch.Trim())") 'Checking pushed branch')
        if ($remoteBranch.Count -ne 1) { throw 'Could not find exactly one matching branch on origin.' }
        $remoteHead = ($remoteBranch[0] -split '\s+')[0]
        if ($head.Trim() -cne $remoteHead) { throw 'Current commit is not pushed to origin.' }

        $changelog = Get-Content -LiteralPath (Join-Path $Root 'CHANGELOG.md') -Raw -Encoding UTF8
        $escapedVersion = [regex]::Escape($version)
        $notesMatch = [regex]::Match($changelog, "(?ms)^##[ \t]+$escapedVersion[ \t]*\r?\n(.*?)(?=^##[ \t]|\z)")
        if (-not $notesMatch.Success) { throw "CHANGELOG.md has no section for version $version." }
        $notes = $notesMatch.Groups[1].Value.Trim()
        if ([string]::IsNullOrWhiteSpace($notes)) { throw "CHANGELOG.md section for $version is empty." }

        if ($Publish) {
            # Do not rebuild: ZIP timestamps would change the candidate's hash.
            $ArchivePath = Test-ReleaseAssets $version
            $release = Get-Release $tag
            Assert-Release $release $tag
            if (-not $release.isDraft -and -not $release.isPrerelease) {
                throw 'Release is already stable; refusing to edit it.'
            }
            $tagCommit = [string](Invoke-RepoGit @('rev-parse', "$tag^{commit}") 'Reading release tag commit')
            $remoteTags = @(Invoke-RepoGit @('ls-remote', '--tags', 'origin', "refs/tags/$tag^{}") 'Checking annotated release tag')
            if ($tagCommit.Trim() -cne $head.Trim() -or $remoteTags.Count -ne 1 -or
                ($remoteTags[0] -split '\s+')[0] -cne $head.Trim()) {
                throw 'Local and remote annotated release tags must match the current commit.'
            }
            Compare-RemoteAssets $tag
            $null = Invoke-Checked 'gh' @('release', 'edit', $tag, '--repo', $Repo,
                '--draft=false', '--prerelease=false', '--latest', '--verify-tag') 'Publishing stable release'
            $published = Get-Release $tag
            Assert-Release $published $tag
            if ($published.isDraft -or $published.isPrerelease) { throw 'Stable release verification failed.' }
            Write-Host "Published stable release with unchanged assets: https://github.com/$Repo/releases/tag/$tag"
            return
        }

        $existingTag = @(Invoke-RepoGit @('tag', '--list', $tag) 'Checking local tag')
        if ($existingTag.Count -gt 0) { throw "Local tag $tag already exists; refusing to overwrite it." }
        $remoteTags = @(Invoke-RepoGit @('ls-remote', '--tags', 'origin', "refs/tags/$tag", "refs/tags/$tag^{}") 'Checking remote tag')
        if ($remoteTags.Count -gt 0) { throw "Remote tag $tag already exists; refusing to overwrite it." }
        $releaseListOutput = Invoke-Checked 'gh' @('release', 'list', '--repo', $Repo, '--limit', '1000', '--json', 'tagName') 'Checking existing GitHub Releases'
        $releases = ($releaseListOutput -join "`n") | ConvertFrom-Json
        if (@($releases | Where-Object { $_.tagName -ceq $tag }).Count -gt 0) {
            throw "A GitHub Release for $tag already exists; refusing to replace it."
        }
    }

    $ArchivePath = Build-ReleaseAssets $version
    $archiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Built: $([IO.Path]::GetFileName($ArchivePath))"
    Write-Host "SHA256: $archiveHash"

    if ($BuildOnly) {
        Write-Host 'Build-only mode: no GitHub changes were made.'
        return
    }

    $dirty = @(Invoke-RepoGit @('status', '--porcelain', '--untracked-files=all') 'Rechecking working tree after build')
    $builtHead = [string](Invoke-RepoGit @('rev-parse', 'HEAD') 'Rechecking built commit')
    if ($dirty.Count -gt 0 -or $builtHead.Trim() -cne $head.Trim()) {
        throw 'Source checkout changed during the build; refusing to tag it.'
    }
    $null = Invoke-RepoGit @('tag', '-a', $tag, $head.Trim(), '-m', "USB PD UFP $tag") 'Creating annotated tag'
    $null = Invoke-RepoGit @('push', 'origin', $tag) 'Pushing release tag'
    # Preserve Unicode, quotes and multiline changelog text on Windows PS 5.1.
    $notesFile = [IO.Path]::GetTempFileName()
    try {
        [IO.File]::WriteAllText($notesFile, $notes, [Text.UTF8Encoding]::new($false))
        $null = Invoke-Checked 'gh' @(
            'release', 'create', $tag, $ArchivePath, $ChecksumsPath,
            '--repo', $Repo, '--title', "USB PD UFP $tag", '--notes-file', $notesFile,
            '--draft', '--prerelease', '--verify-tag'
        ) 'Creating draft GitHub Release'
    }
    finally { Remove-Item -LiteralPath $notesFile -Force }

    $release = Get-Release $tag
    Assert-Release $release $tag
    if (-not $release.isDraft -or -not $release.isPrerelease) {
        throw 'GitHub draft verification failed; inspect the release before retrying.'
    }

    Compare-RemoteAssets $tag
    if ($Candidate) {
        $null = Invoke-Checked 'gh' @('release', 'edit', $tag, '--repo', $Repo, '--draft=false', '--verify-tag') 'Publishing GitHub Release'
        $published = Get-Release $tag
        Assert-Release $published $tag
        if ($published.isDraft -or -not $published.isPrerelease) {
            throw 'Release did not verify as a published prerelease.'
        }
        Write-Host "Prerelease ready: https://github.com/$Repo/releases/tag/$tag"
        Write-Host 'Promote after testing with: .\release.ps1 -Publish'
    }
    else {
        Write-Host "Draft ready for review: https://github.com/$Repo/releases/tag/$tag"
        Write-Host 'Publish stable after review with: .\release.ps1 -Publish'
    }
}
catch {
    Write-Error -Message $_ -ErrorAction Continue
    exit 1
}
finally {
    Pop-Location
    [Environment]::SetEnvironmentVariable('GIT_TERMINAL_PROMPT', $savedGitPrompt, 'Process')
}
