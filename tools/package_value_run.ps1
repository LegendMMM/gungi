param(
    [string]$Name = "",
    [string]$DatasetPath = "value_data_v2_100k.csv",
    [string]$ModelPath = "models\value_weights_v2.bin",
    [string]$TrainLogPath = "train_value_v2.csv",
    [string]$EvalLogPath = "value_ai_eval_v2.csv",
    [string]$EvalSummaryPath = "value_ai_eval_v2_summary.csv"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "value_model_v2_" + (Get-Date -Format "yyyyMMdd_HHmmss")
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$analysis = Join-Path $root "analysis"
$outDir = Join-Path $analysis $Name

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$files = @(
    @{ Source = $DatasetPath; Target = "dataset.csv"; Required = $true },
    @{ Source = $ModelPath; Target = "model.bin"; Required = $true },
    @{ Source = $TrainLogPath; Target = "train_log.csv"; Required = $true },
    @{ Source = $EvalLogPath; Target = "eval_games.csv"; Required = $false },
    @{ Source = $EvalSummaryPath; Target = "eval_summary.csv"; Required = $false }
)

$copied = @()
foreach ($file in $files) {
    $sourcePath = Join-Path $root $file.Source
    if (Test-Path -LiteralPath $sourcePath) {
        $targetPath = Join-Path $outDir $file.Target
        Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
        $copied += $file.Target
    } elseif ($file.Required) {
        throw "Required file not found: $($file.Source)"
    }
}

$manifestPath = Join-Path $outDir "manifest.json"
$manifest = [ordered]@{
    name = $Name
    created_at = (Get-Date).ToString("s")
    value_model_version = 2
    feature_count = 192
    dataset = $DatasetPath
    model = $ModelPath
    train_log = $TrainLogPath
    eval_log = $EvalLogPath
    eval_summary = $EvalSummaryPath
    copied_files = $copied
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding ascii

$reportPath = Join-Path $outDir "REPORT.md"
$report = @'
# Value Model V2 Run

## Purpose

This package keeps one Value-Minimax V2 run together for analysis.

## Contents

- `dataset.csv`: V2 value training dataset.
- `model.bin`: trained V2 model weights.
- `train_log.csv`: epoch-level train/validation MSE.
- `eval_games.csv`: per-game evaluation log, if available.
- `eval_summary.csv`: aggregate evaluation metrics, if available.
- `manifest.json`: source paths and run metadata.
- `SHA256SUMS.txt`: file hashes for integrity checks.

## V2 Notes

- Model schema: V2.
- Default model path: `models/value_weights_v2.bin`.
- `ply_count` is not used as a value feature.
- Search uses a fixed top-K cap with a bounded tactical safety bucket.
- Evaluation supports random openings and multiple seeds.

## Recommended Commands

```powershell
tools\gen_value_data.exe 100000 value_data_v2_100k.csv 1
tools\train_value.exe value_data_v2_100k.csv models\value_weights_v2.bin 15 0.01 0.00001 train_value_v2.csv
tools\eval_value_ai.exe --games 100 --model models\value_weights_v2.bin --seed 1 --seed-count 4 --depth 2 --top-k 12 --max-ply 3000 --opening-min 6 --opening-max 12 --output value_ai_eval_v2.csv --summary value_ai_eval_v2_summary.csv
powershell -ExecutionPolicy Bypass -File tools\package_value_run.ps1 -Name value_model_v2_100k_e15
```
'@
$report | Set-Content -LiteralPath $reportPath -Encoding ascii

$hashLines = @()
Get-ChildItem -LiteralPath $outDir -File | Sort-Object Name | ForEach-Object {
    if ($_.Name -ne "SHA256SUMS.txt") {
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $hashLines += "$hash  $($_.Name)"
    }
}
$hashLines | Set-Content -LiteralPath (Join-Path $outDir "SHA256SUMS.txt") -Encoding ascii

$zipPath = Join-Path $analysis ($Name + ".zip")
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $outDir "*") -DestinationPath $zipPath -Force

Write-Host "Packaged $outDir"
Write-Host "Zip $zipPath"
