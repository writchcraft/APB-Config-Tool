param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$Destination = (Join-Path $env:TEMP 'APB-Localization-Helper'),
    [switch]$Publish,
    [string]$RemoteUrl = 'https://github.com/writchcraft/APB-Localization-Helper.git',
    [string]$Branch = 'main',
    [string]$CommitMessage = 'Public release',
    [string]$AuthorName = 'writchcraft',
    [string]$AuthorEmail = 'writchcraft@users.noreply.github.com'
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    [System.IO.Path]::GetFullPath($Path)
}

$repoRoot = Get-FullPath $SourceRoot
$destRoot = Get-FullPath $Destination

$repoRootCheck = $repoRoot.TrimEnd('\', '/')
$destRootCheck = $destRoot.TrimEnd('\', '/')

if ($destRootCheck -eq $repoRootCheck -or
    $destRootCheck.StartsWith($repoRootCheck + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must not be inside the source repository."
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is required to build the public release mirror."
}

if (Test-Path -LiteralPath $destRoot) {
    Remove-Item -LiteralPath $destRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $destRoot -Force | Out-Null

$tempZip = Join-Path $env:TEMP ("apb-public-" + [guid]::NewGuid().ToString('N') + ".zip")

try {
    $env:FILTER_BRANCH_SQUELCH_WARNING = '1'

    & git -C $repoRoot archive --worktree-attributes --format=zip --output $tempZip HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "git archive failed."
    }

    Expand-Archive -LiteralPath $tempZip -DestinationPath $destRoot -Force

    $examples = Join-Path $repoRoot 'PremadeConfigsEXAMPLES'
    if (Test-Path -LiteralPath $examples) {
        Copy-Item -LiteralPath $examples -Destination (Join-Path $destRoot 'PremadeConfigsEXAMPLES') -Recurse -Force
    }

    if ($Publish) {
        $publishRoot = Join-Path $env:TEMP ("apb-public-git-" + [guid]::NewGuid().ToString('N'))
        try {
            & git clone --mirror $repoRoot $publishRoot
            if ($LASTEXITCODE -ne 0) {
                throw "git clone --mirror failed."
            }

            $filterPaths = @(
                'CMakeLists.txt'
                '.idea'
                '.vscode'
                'cmake-build-release'
                'APBConfigHelper.exe.lnk'
                'desktop.ini'
            )
            $filterCommand = "git rm -r --cached --ignore-unmatch -- " + ($filterPaths -join ' ')
            & git -C $publishRoot filter-branch --force --index-filter $filterCommand --tag-name-filter cat -- --all
            if ($LASTEXITCODE -ne 0) {
                throw "git filter-branch failed."
            }

            $originalRefs = & git -C $publishRoot for-each-ref --format='%(refname)' refs/original
            foreach ($ref in $originalRefs) {
                if ($ref) {
                    & git -C $publishRoot update-ref -d $ref
                }
            }

            $remoteRefs = & git -C $publishRoot for-each-ref --format='%(refname)' refs/remotes
            foreach ($ref in $remoteRefs) {
                if ($ref) {
                    & git -C $publishRoot update-ref -d $ref
                }
            }

            & git -C $publishRoot reflog expire --expire=now --all
            & git -C $publishRoot gc --prune=now --aggressive

            & git -C $publishRoot remote set-url origin $RemoteUrl
            if ($LASTEXITCODE -ne 0) {
                throw "git remote set-url failed."
            }

            & git -C $publishRoot push --force --mirror origin
            if ($LASTEXITCODE -ne 0) {
                throw "git push failed."
            }
        }
        finally {
            if (Test-Path -LiteralPath $publishRoot) {
                Remove-Item -LiteralPath $publishRoot -Recurse -Force
            }
        }
    }
}
finally {
    if (Test-Path -LiteralPath $tempZip) {
        Remove-Item -LiteralPath $tempZip -Force
    }
}

Write-Host "Public release mirror created at: $destRoot"
