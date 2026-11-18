# Building and publishing a release

## Prerequisites

- A clean commit pushed to the matching branch on `origin`.
- Windows with devkitPro/devkitA64 and libnx installed at `C:\devkitPro`.
- Windows PowerShell 5.1 or PowerShell 7, Git and GitHub CLI (`gh`).
- GitHub CLI authenticated for `vault-boy-13/usb-pd-ufp`.

Run `gh auth login --hostname github.com` if needed. Git HTTPS operations
use GitHub CLI credentials for this script only, without changing global Git
settings. SSH remotes use your existing SSH configuration. Git terminal
password prompts are disabled; authentication failures stop the script.

## Build and test the install archive

Commands below assume this directory. Calling the script by path from a
different directory also works; Git always targets this project.

```powershell
.\release.ps1 -BuildOnly
.\tests\test_release.ps1
```

The script runs the project tests, forces a full sysmodule rebuild, and creates
`usb-pd-ufp-<version>.zip` and `SHA256SUMS.txt`. The version is read from
`source/main.c` by both the release script and packager. The package has an
exact, verified file list and includes the README, project license and libnx
notice. NPDM, ExeFS and archive CRC are verified by the packager. Both LF and
CRLF version definitions are supported. Build output and archives are ignored
by Git. The offline release tests use mock Git/GitHub commands; they do not
create real tags or releases.

## Create and publish a GitHub Release

Update `VERSION` in `source/main.c` and add a matching `## <version>` section
to `CHANGELOG.md` with nonempty notes. Commit and push all release changes,
including scripts, tests and packaging changes, then run:

```powershell
.\release.ps1
```

The script checks the repository, pushed commit, version, archive contents and
checksum. It pushes an annotated `v<version>` tag and creates a draft
prerelease with the install archive and checksum file. It downloads the
uploaded assets and verifies that they match the local files.

To create and expose a prerelease in one run instead, use:

```powershell
.\release.ps1 -Candidate
```

To expose an already created draft as a prerelease after review:

```powershell
gh release edit v<version> --repo vault-boy-13/usb-pd-ufp --draft=false --prerelease --verify-tag
```

After reviewing the draft or testing the prerelease, publish stable with:

```powershell
.\release.ps1 -Publish
```

`-Publish` promotes an existing draft or prerelease and marks it Latest. It
does not rebuild, create a tag or replace assets. It requires the original
local ZIP and checksum, a clean pushed checkout at the tagged commit,
matching local and remote annotated tags, exactly the two expected assets,
and byte-for-byte matching downloads. An already stable release is refused.

Keep the ZIP and checksum from the candidate you intend to publish: ZIP
timestamps change on rebuild. If local assets are lost or rebuilt, download
the original archive and checksum from the release before promotion.

`-BuildOnly`, `-Candidate` and `-Publish` are mutually exclusive.
`-Publish` promotes an existing release; it does not create a new one.

The script refuses to replace an existing local or remote tag or release
during creation.
If it fails after pushing the tag, inspect GitHub before retrying. Build-only
mode does not require GitHub CLI and does not make GitHub changes.
Do not delete or replace a published tag just to rerun the script.

A release inherits repository visibility: publishing in a private repository
does not make its files public.
