param(
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [string]$Language = 'en-US'
)

$ErrorActionPreference = 'Stop'
$runs = (Resolve-Path -LiteralPath $RunDirectory).Path
python (Join-Path $PSScriptRoot 'prepare_ocr.py') $runs
if ($LASTEXITCODE -ne 0) { throw "Could not prepare OCR crops." }

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType=WindowsRuntime]
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Media.Ocr.OcrResult, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Globalization.Language, Windows.Globalization, ContentType=WindowsRuntime]

$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1

function Complete-Operation($Operation, [Type]$ResultType) {
    $task = $asTask.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait()
    $task.Result
}

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new($Language))
if ($null -eq $engine) { throw "Windows OCR language $Language is unavailable." }

function Read-Crop([string]$Name) {
    $path = Join-Path (Join-Path $runs 'ocr') $Name
    $file = Complete-Operation ([Windows.Storage.StorageFile]::GetFileFromPathAsync($path)) ([Windows.Storage.StorageFile])
    $stream = Complete-Operation ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    try {
        $decoder = Complete-Operation ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        $bitmap = Complete-Operation ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
        try {
            $result = Complete-Operation ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
            return $result.Text
        } finally {
            $bitmap.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$room = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $runs 'room.json') | ConvertFrom-Json
$blank = Read-Crop 'blank.png'
$readings = @(foreach ($sample in $room.observations) {
    $text = Read-Crop $sample.file
    $matched = ($text -replace '\s+', '').ToUpperInvariant() -ceq ($sample.expected -replace '\s+', '')
    [ordered]@{ file = $sample.file; expected = $sample.expected; recognized = $text; matched = $matched }
})
$matchesCount = @($readings | Where-Object { $_.matched }).Count
$summary = [ordered]@{
    language = $Language
    blank = $blank
    matches = $matchesCount
    count = $readings.Count
    readings = $readings
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $runs 'recognition.json')
Write-Host "Gate 4 OCR read $matchesCount of $($readings.Count) titles exactly. Blank control read '$blank'."
if ($matchesCount -ne 64 -or $readings.Count -ne 64 -or $blank -or $room.failures.Count) { exit 1 }
