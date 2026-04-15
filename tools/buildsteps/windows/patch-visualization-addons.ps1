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
  $updated = $content

  $updated = $updated -replace '&__floor', '&floor'
  $updated = $updated -replace '&__ceil', '&ceil'

  $updated = [regex]::Replace(
    $updated,
    '(?m)^(?!\s*#ifndef _MSC_VER\s*$)\s*(static\s+double\s+__floor\s*\([^\r\n]*\)\s*\{[^\r\n]*\}\s*)$',
    "#ifndef _MSC_VER`r`n`$1`r`n#endif"
  )

  $updated = [regex]::Replace(
    $updated,
    '(?m)^(?!\s*#ifndef _MSC_VER\s*$)\s*(static\s+double\s+__ceil\s*\([^\r\n]*\)\s*\{[^\r\n]*\}\s*)$',
    "#ifndef _MSC_VER`r`n`$1`r`n#endif"
  )

  if ($updated -ne $content) {
    Set-Content -LiteralPath $target.FullName -Value $updated -NoNewline
    Write-Host "Patched $($target.FullName)"
  }
  else {
    Write-Host "No changes needed for $($target.FullName)"
  }
}
