# Builds the speaker firmware (native_bt/pure_sink) with the bundled ESP-IDF 6.1.
# Windows PowerShell. Needs the ESP-IDF 6.1 tools installed once: esp-idf\install.ps1 esp32
# Output: native_bt\pure_sink\build\pure_sink.bin
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'native_bt\pure_sink'
$env:IDF_PATH = Join-Path $repo 'esp-idf'
. (Join-Path $env:IDF_PATH 'export.ps1') | Out-Null
$env:IDF_COMPONENT_MANAGER = '0'
$env:IDF_CCACHE_ENABLE = '1'
python (Join-Path $env:IDF_PATH 'tools\idf.py') --ccache -C $project -B (Join-Path $project 'build') build
$code = $LASTEXITCODE
if ($code -eq 0) {
    # The image goes in the 4 MB "factory" partition (partitions.csv); check it fits.
    python (Join-Path $env:IDF_PATH 'components\partition_table\check_sizes.py') --offset 0x8000 partition --type app `
        (Join-Path $project 'build\partition_table\partition-table.bin') (Join-Path $project 'build\pure_sink.bin')
}
exit $code
