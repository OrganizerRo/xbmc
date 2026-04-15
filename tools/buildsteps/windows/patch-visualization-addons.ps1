param(
  [string]$AddonsBuildPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AddonsBuildPath)) {
  throw "AddonsBuildPath parameter is required."
}

if (-not (Test-Path -LiteralPath $AddonsBuildPath)) {
  Write-Host "Addon build path not found, skipping visualization patching: $AddonsBuildPath"
  exit 0
}

$targets = Get-ChildItem -Path $AddonsBuildPath -Filter "nseel-compiler.c" -File -Recurse | Where-Object {
  $_.FullName -match 'visualization\.milkdrop2?'
}

if (-not $targets) {
  Write-Host "No milkdrop nseel-compiler.c files found under $AddonsBuildPath"
  exit 0
}

foreach ($target in $targets) {
  $content = Get-Content -LiteralPath $target.FullName -Raw
  $lineEnding = if ($content.Contains("`r`n")) { "`r`n" } else { "`n" }
  $updated = $content

  $updated = $updated -replace '&__floor', '&floor'
  $updated = $updated -replace '&__ceil', '&ceil'

  # The milkdrop nseel wrappers in affected sources are single-line definitions.
  if ($updated -notmatch '(?ms)#ifndef\s+_MSC_VER\s*[\r\n]+\s*static\s+double\s+__floor\s*\(') {
    $updated = [regex]::Replace(
      $updated,
      '(?m)^\s*(static\s+double\s+__floor\s*\([^\r\n]*\)\s*\{[^\r\n]*\}\s*)$',
      "#ifndef _MSC_VER$lineEnding`$1$lineEnding#endif"
    )
  }

  if ($updated -notmatch '(?ms)#ifndef\s+_MSC_VER\s*[\r\n]+\s*static\s+double\s+__ceil\s*\(') {
    $updated = [regex]::Replace(
      $updated,
      '(?m)^\s*(static\s+double\s+__ceil\s*\([^\r\n]*\)\s*\{[^\r\n]*\}\s*)$',
      "#ifndef _MSC_VER$lineEnding`$1$lineEnding#endif"
    )
  }

  if ($updated -ne $content) {
    Set-Content -LiteralPath $target.FullName -Value $updated -NoNewline
    Write-Host "Patched $($target.FullName)"
  }
  else {
    Write-Host "No changes needed for $($target.FullName)"
  }
}
