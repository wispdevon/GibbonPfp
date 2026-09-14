param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
if (-not $env:WINDOWS_CERTIFICATE_BASE64) {
    Write-Host 'No Windows signing certificate configured; package is unsigned.'
    exit 0
}
$certificatePath = Join-Path $env:RUNNER_TEMP 'gibbon-signing.pfx'
try {
    [IO.File]::WriteAllBytes($certificatePath, [Convert]::FromBase64String($env:WINDOWS_CERTIFICATE_BASE64))
    $signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' | Sort-Object FullName | Select-Object -Last 1
    if (-not $signtool) { throw 'Windows SDK signtool was not found.' }
    & $signtool.FullName sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /f $certificatePath /p $env:WINDOWS_CERTIFICATE_PASSWORD $Executable
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing failed.' }
} finally {
    Remove-Item $certificatePath -Force -ErrorAction SilentlyContinue
}
