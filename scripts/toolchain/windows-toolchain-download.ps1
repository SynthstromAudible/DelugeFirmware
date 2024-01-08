Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = "tls12, tls11, tls"
# TODO: fix
$download_dir = (Get-Item "$PSScriptRoot\..\..").FullName
$toolchain_version = $args[0]
$toolchain_target_path = $args[1]

$toolchain_url = "https://github.com/SynthstromAudible/dbt-toolchain/releases/download/v$toolchain_version/dbt-toolchain-$toolchain_version-win32-x86_64.zip"
$toolchain_dist_folder = "win32-x86_64"
$toolchain_zip = "$toolchain_dist_folder-$toolchain_version.zip"

$toolchain_zip_temp_path = "$download_dir\$toolchain_zip"
$toolchain_dist_temp_path = "$download_dir\$toolchain_dist_folder"

if (!(Test-Path -Path "$toolchain_zip_temp_path" -PathType Leaf)) {
    Write-Host -NoNewline "Downloading Windows toolchain.."
    $wc = New-Object net.webclient
    $wc.Downloadfile("$toolchain_url", "$toolchain_zip_temp_path")
    Write-Host "done!"
}

if (!(Test-Path -LiteralPath "$toolchain_target_path\..")) {
    New-Item "$toolchain_target_path\.." -ItemType Directory -Force
}

Write-Host -NoNewline "Extracting Windows toolchain.."
# This is faster than Expand-Archive
Add-Type -Assembly "System.IO.Compression.Filesystem"
[System.IO.Compression.ZipFile]::ExtractToDirectory("$toolchain_zip_temp_path", "$download_dir")
# Expand-Archive -LiteralPath "$toolchain_zip_temp_path" -DestinationPath "$download_dir"

Write-Host -NoNewline "moving.."
Move-Item -LiteralPath "$toolchain_dist_temp_path" -Destination "$toolchain_target_path" -Force
Write-Host "done!"

Write-Host -NoNewline "Cleaning up temporary files.."
Remove-Item -LiteralPath "$toolchain_zip_temp_path" -Force
Write-Host "done!"

