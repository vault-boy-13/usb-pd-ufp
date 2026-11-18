# Offline integration tests: only Git/GitHub and compilation are replaced.
# Archive verification and release orchestration use the production script.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('usb-pd-ufp-test-' + [Guid]::NewGuid().ToString())
$null = New-Item -ItemType Directory -Path (Join-Path $fixture 'source') -Force
try {
    $script = Get-Content (Join-Path $root 'release.ps1') -Raw -Encoding UTF8
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseInput($script, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    $build = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Build-ReleaseAssets'
    }, $true)
    $replacement = @'
function Build-ReleaseAssets {
    param([string]$Version)
    Add-Content -LiteralPath $env:RELEASE_TEST_LOG -Value 'BUILD'
    Test-ReleaseAssets $Version
}
'@
    $script = $script.Substring(0, $build.Extent.StartOffset) + $replacement + $script.Substring($build.Extent.EndOffset)
    [IO.File]::WriteAllText((Join-Path $fixture 'release.ps1'), $script)
    $versionSource = Get-Content (Join-Path $root 'source/main.c') -Raw
    $version = [regex]::Match($versionSource, '#define VERSION "([\d.]+)"').Groups[1].Value
    # Exercise CRLF version parsing on every scenario.
    [IO.File]::WriteAllText((Join-Path $fixture 'source/main.c'), "#define VERSION `"$version`"`r`n")
    foreach ($name in @("usb-pd-ufp-$version.zip", 'SHA256SUMS.txt', 'CHANGELOG.md')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $fixture
    }
    $runner = @'
param([string]$Scenario, [string]$Mode, [string]$Version)
$ErrorActionPreference = 'Stop'
$global:testRoot = $PSScriptRoot
$global:testTag = "v$Version"
$global:testArchive = "usb-pd-ufp-$Version.zip"
$global:testDraft = $Scenario -ne 'promote' -and $Scenario -ne 'stable'
$global:testPrerelease = $Scenario -ne 'stable'
$global:testScenario = $Scenario
$global:testHead = '1111111111111111111111111111111111111111'
function git {
    $global:LASTEXITCODE = 0
    $a = @($args)
    if ($a[0] -cne '-C' -or $a[1] -cne $global:testRoot -or
        $a[3] -cne 'credential.helper=' -or $a[5] -cne 'credential.https://github.com.helper=!gh auth git-credential') {
        throw 'Git must use explicit project path and scoped gh credentials.'
    }
    $a = @($a | Select-Object -Skip 6)
    $call = $a -join ' '
    Add-Content $env:RELEASE_TEST_LOG "GIT $call"
    switch -Wildcard ($call) {
        'remote get-url origin' { 'https://github.com/vault-boy-13/usb-pd-ufp.git'; return }
        'branch --show-current' { 'main'; return }
        'status *' { if ($global:testScenario -eq 'dirty') { ' M README.md' }; return }
        'rev-parse *' { $global:testHead; return }
        'ls-remote --heads *' {
            if ($global:testScenario -eq 'unpushed') { '2222222222222222222222222222222222222222 refs/heads/main' }
            else { "$global:testHead refs/heads/main" }; return
        }
        'tag --list *' { if ($global:testScenario -eq 'existing') { $global:testTag }; return }
        'ls-remote --tags *' {
            if ($Mode -eq 'Publish') { "$global:testHead refs/tags/$global:testTag^{}" }; return
        }
        'tag -a *' { return }
        'push *' { if ($global:testScenario -eq 'push-failed') { $global:LASTEXITCODE = 1 }; return }
        default { throw "Unexpected Git call: $call" }
    }
}
function gh {
    $global:LASTEXITCODE = 0
    $a = @($args)
    $call = $a -join ' '
    Add-Content $env:RELEASE_TEST_LOG "GH $call"
    switch -Wildcard ($call) {
        'auth status *' { return }
        'release list *' {
            if ($global:testScenario -eq 'release-existing') { '[{"tagName":"' + $global:testTag + '"}]' }
            else { '[]' }; return
        }
        'release create *' {
            if ($call -notlike '*--draft --prerelease --verify-tag*') { throw 'Expected draft prerelease.' }
            $notesPath = $a[[Array]::IndexOf($a, '--notes-file') + 1]
            if (-not (Test-Path -LiteralPath $notesPath)) { throw 'Missing release notes file.' }; return
        }
        'release view *' {
            @{ tagName=$global:testTag; isDraft=$global:testDraft; isPrerelease=$global:testPrerelease
                assets=@(@{name=$global:testArchive}, @{name='SHA256SUMS.txt'}) } | ConvertTo-Json -Depth 5 -Compress
            return
        }
        'release download *' {
            $dir = $a[[Array]::IndexOf($a, '--dir') + 1]
            foreach ($name in @($global:testArchive, 'SHA256SUMS.txt')) {
                Copy-Item -LiteralPath (Join-Path $global:testRoot $name) -Destination $dir
            }
            if ($global:testScenario -like 'mismatch*') { Add-Content (Join-Path $dir $global:testArchive) 'corrupt' }
            return
        }
        'release edit *' {
            $global:testDraft = $false
            if ($call -like '*--prerelease=false*') { $global:testPrerelease = $false }; return
        }
        default { throw "Unexpected gh call: $call" }
    }
}
$before = (Get-Location).Path
$env:GIT_TERMINAL_PROMPT = 'test-original'
$options = @{}
if ($Mode -eq 'Candidate') { $options.Candidate = $true }
if ($Mode -eq 'Publish') { $options.Publish = $true }
if ($Mode -eq 'Invalid') { $options.BuildOnly = $true; $options.Publish = $true }
& (Join-Path $PSScriptRoot 'release.ps1') @options
$releaseCode = $LASTEXITCODE
if ((Get-Location).Path -cne $before -or $env:GIT_TERMINAL_PROMPT -cne 'test-original') {
    throw 'Caller location/environment was not restored.'
}
'RESTORED'
exit $releaseCode
'@
    $runnerPath = Join-Path $fixture 'runner.ps1'
    [IO.File]::WriteAllText($runnerPath, $runner)
    $shell = (Get-Process -Id $PID).Path
    $savedLog = [Environment]::GetEnvironmentVariable('RELEASE_TEST_LOG', 'Process')
    try {
        foreach ($case in @(
            @('draft', 'Draft', 0), @('candidate', 'Candidate', 0),
            @('publish-draft', 'Publish', 0), @('promote', 'Publish', 0),
            @('dirty', 'Draft', 1), @('unpushed', 'Draft', 1), @('existing', 'Draft', 1),
            @('mismatch', 'Publish', 1), @('push-failed', 'Draft', 1),
            @('mismatch-draft', 'Candidate', 1), @('release-existing', 'Draft', 1),
            @('stable', 'Publish', 1), @('invalid', 'Invalid', 1)
        )) {
            $env:RELEASE_TEST_LOG = Join-Path $fixture ($case[0] + '.log')
            $ErrorActionPreference = 'Continue'
            $output = & $shell -NoProfile -ExecutionPolicy Bypass -File $runnerPath $case[0] $case[1] $version 2>&1
            $code = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($code -ne $case[2]) { throw "Case $($case[0]): expected $($case[2]), got $code`n$($output -join "`n")" }
            $log = if (Test-Path $env:RELEASE_TEST_LOG) { Get-Content $env:RELEASE_TEST_LOG -Raw } else { '' }
            if ($case[2] -eq 0 -and ($output -join "`n") -notlike '*RESTORED*') { throw 'Missing restoration check.' }
            if ($case[1] -eq 'Publish' -and $log -match '(?m)^(BUILD|GIT tag -a|GIT push|GH release create)') {
                throw 'Promotion must not rebuild, push or create a release.'
            }
            if ($case[0] -in @('dirty', 'unpushed', 'existing', 'release-existing', 'invalid', 'stable', 'mismatch') -and
                $log -match '(?m)^(GIT tag -a|GIT push|GH release create|GH release edit)') { throw 'Unsafe mutation after failed validation.' }
            if ($case[0] -eq 'mismatch-draft' -and $log -match '(?m)^GH release edit') { throw 'Candidate exposed with corrupt uploads.' }
            if ($case[0] -eq 'push-failed' -and $log -match '(?m)^GH release create') { throw 'Created release after failed push.' }
            if ($case[0] -eq 'draft' -and $log -match '(?m)^GH release edit') { throw 'Draft was exposed.' }
            if ($case[0] -eq 'candidate' -and $log -notmatch '(?m)^GH release edit .*--draft=false') { throw 'Candidate was not exposed.' }
            if ($case[2] -eq 0 -and $log -notmatch '(?m)^GH release download') { throw 'Assets were not downloaded for verification.' }
            Write-Host "PASS: $($case[0])"
        }
    }
    finally { [Environment]::SetEnvironmentVariable('RELEASE_TEST_LOG', $savedLog, 'Process') }
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolved -Leaf) -notlike 'usb-pd-ufp-test-*') { throw 'Unexpected fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
