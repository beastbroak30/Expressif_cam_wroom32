param(
  [string]$TargetIp = "REDACTED_IP",
  [int]$TargetPort = 3232,
  [string]$OutputDir = "G:\ota",
  [string]$Fqbn = "esp32:esp32:esp32cam",
  [string]$BoardOptions = "PartitionScheme=min_spiffs",
  [string]$Distro,
  [string]$LinuxSketchDir,
  [switch]$SkipUpload,
  [switch]$UsbUpload,
  [string]$UsbPort = "/dev/ttyUSB0"
)

$ErrorActionPreference = "Stop"

function Get-WslSketchContext {
  param(
    [string]$ScriptRoot,
    [string]$ConfiguredDistro,
    [string]$ConfiguredLinuxSketchDir
  )

  if ($ConfiguredDistro -and $ConfiguredLinuxSketchDir) {
    return [pscustomobject]@{
      Distro = $ConfiguredDistro
      LinuxSketchDir = $ConfiguredLinuxSketchDir
      WindowsSketchDir = (Resolve-Path -LiteralPath $ScriptRoot).Path
    }
  }

  $resolvedRoot = (Resolve-Path -LiteralPath $ScriptRoot).Path -replace '^[^:]+::', ''
  if ($resolvedRoot -match '^\\\\wsl\.localhost\\([^\\]+)\\(.+)$') {
    return [pscustomobject]@{
      Distro = $Matches[1]
      LinuxSketchDir = '/' + ($Matches[2] -replace '\\', '/')
      WindowsSketchDir = $resolvedRoot
    }
  }

  throw "Unable to infer the WSL distro and Linux sketch path from $resolvedRoot. Pass -Distro and -LinuxSketchDir explicitly."
}

function Get-WindowsPython {
  $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
  if ($launcher) {
    return [pscustomobject]@{ Path = $launcher.Source; UseLauncher = $true }
  }

  $python = Get-Command python.exe -ErrorAction SilentlyContinue
  if ($python) {
    return [pscustomobject]@{ Path = $python.Source; UseLauncher = $false }
  }

  throw "Python was not found on Windows. Install Python or the Python launcher."
}

function Get-EspotaPath {
  $searchRoots = @(
    "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32",
    "$env:USERPROFILE\.arduino15\packages\esp32\hardware\esp32"
  )

  foreach ($root in $searchRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
      continue
    }

    $match = Get-ChildItem -Path $root -Filter espota.py -Recurse -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending |
      Select-Object -First 1 -ExpandProperty FullName

    if ($match) {
      return $match
    }
  }

  throw "espota.py was not found in the Windows Arduino ESP32 core installation."
}

function Get-HostIpForTarget {
  param(
    [string]$IpAddress,
    [int]$Port
  )

  $client = [System.Net.Sockets.UdpClient]::new()
  try {
    $client.Connect($IpAddress, $Port)
    return ([System.Net.IPEndPoint]$client.Client.LocalEndPoint).Address.IPAddressToString
  }
  finally {
    $client.Dispose()
  }
}

function Invoke-WslBuild {
  param(
    [string]$BuildDistro,
    [string]$SketchDir,
    [string]$BuildDir,
    [string]$BoardFqbn,
    [string]$BoardOptionString
  )

  $command = "cd $SketchDir && mkdir -p $BuildDir && arduino-cli compile --fqbn $BoardFqbn --board-options $BoardOptionString --output-dir $BuildDir ."

  Write-Host "Building in WSL distro '$BuildDistro'..."
  & wsl -d $BuildDistro bash -lc $command
  if ($LASTEXITCODE -ne 0) {
    throw "WSL build failed with exit code $LASTEXITCODE."
  }
}

function Invoke-WslUsbUpload {
  param(
    [string]$BuildDistro,
    [string]$SketchDir,
    [string]$BoardFqbn,
    [string]$BoardOptionString,
    [string]$SerialPort
  )

  $command = "cd $SketchDir && arduino-cli upload -p $SerialPort --fqbn $BoardFqbn --board-options $BoardOptionString ."

  Write-Host "Uploading over USB from WSL on $SerialPort ..."
  & wsl -d $BuildDistro bash -lc $command
  if ($LASTEXITCODE -ne 0) {
    throw "WSL USB upload failed with exit code $LASTEXITCODE."
  }
}

$context = Get-WslSketchContext -ScriptRoot $PSScriptRoot -ConfiguredDistro $Distro -ConfiguredLinuxSketchDir $LinuxSketchDir
$sketchName = Split-Path -Leaf $context.LinuxSketchDir
$binaryName = "$sketchName.ino.bin"
$linuxBuildDir = "$($context.LinuxSketchDir)/build"
$windowsBuildDir = Join-Path $context.WindowsSketchDir "build"
$windowsBuiltBinary = Join-Path $windowsBuildDir $binaryName
$windowsOutputBinary = Join-Path $OutputDir $binaryName

Invoke-WslBuild -BuildDistro $context.Distro -SketchDir $context.LinuxSketchDir -BuildDir $linuxBuildDir -BoardFqbn $Fqbn -BoardOptionString $BoardOptions

if (-not (Test-Path -LiteralPath $windowsBuiltBinary)) {
  throw "Compiled binary not found at $windowsBuiltBinary."
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Copy-Item -LiteralPath $windowsBuiltBinary -Destination $windowsOutputBinary -Force
Write-Host "Copied latest binary to $windowsOutputBinary"

if ($UsbUpload) {
  Invoke-WslUsbUpload -BuildDistro $context.Distro -SketchDir $context.LinuxSketchDir -BoardFqbn $Fqbn -BoardOptionString $BoardOptions -SerialPort $UsbPort
  Write-Host "USB upload completed. This refreshes the partition table for OTA-capable layouts."
}

if ($SkipUpload) {
  Write-Host "SkipUpload set. Build and copy completed without OTA upload."
  exit 0
}

$python = Get-WindowsPython
$espota = Get-EspotaPath
$hostIp = Get-HostIpForTarget -IpAddress $TargetIp -Port $TargetPort

Write-Host "Uploading $windowsOutputBinary to ${TargetIp}:${TargetPort} using host IP $hostIp"

if ($python.UseLauncher) {
  $uploadArgs = @('-3', $espota, '-i', $TargetIp, '-I', $hostIp, '-p', "$TargetPort", '-f', $windowsOutputBinary, '-r', '-d')
} else {
  $uploadArgs = @($espota, '-i', $TargetIp, '-I', $hostIp, '-p', "$TargetPort", '-f', $windowsOutputBinary, '-r', '-d')
}

& $python.Path @uploadArgs
if ($LASTEXITCODE -ne 0) {
  throw "OTA upload failed with exit code $LASTEXITCODE."
}

Write-Host "OTA upload completed successfully."