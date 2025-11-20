$TargetDir = "$HOME/bin"

if (-not (Test-Path $TargetDir)) { New-Item -ItemType Directory -Path $TargetDir | Out-Null }

Write-Host "Downloading kpm..."

$Url = "https://github.com/lPrimemaster/kpm/releases/latest/download/kpm.exe"
$temp = Join-Path $env:TEMP "kpm.exe"

Invoke-WebRequest -Uri $Url -OutFile $temp

Move-Item -Force $temp (Join-Path $TargetDir "kpm.exe")

$path = [Environment]::GetEnvironmentVariable("PATH", "User")

if (-not ($path.Split(';') -contains $TargetDir)) {
	[Environment]::SetEnvironmentVariable("PATH", "$TargetDir;$path", "User")
	Write-Host "Added $targetDir to PATH."
}

Write-Host "Successfuly installed kpm."
