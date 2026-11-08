# Special Nights launcher (Windows PowerShell 5.1 or newer).
# Arrow keys move, Space or Enter picks, Esc goes back. Started by play.bat.

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Game = Join-Path $Root 'build-msvc-x86\Release\ctr_native.exe'
$SettingsFile = Join-Path $Root 'play-settings.json'

# Names as the game's --track / --character options know them.
$Tracks = @(
    @{ Name = 'Crash Cove';      Arg = 'crashcove' },
    @{ Name = 'Roo''s Tubes';    Arg = 'roostubes' },
    @{ Name = 'Tiger Temple';    Arg = 'tigertemple' },
    @{ Name = 'Coco Park';       Arg = 'cocopark' },
    @{ Name = 'Mystery Caves';   Arg = 'mysterycaves' },
    @{ Name = 'Blizzard Bluff';  Arg = 'blizzardbluff' },
    @{ Name = 'Sewer Speedway';  Arg = 'sewerspeedway' },
    @{ Name = 'Dingo Canyon';    Arg = 'dingocanyon' },
    @{ Name = 'Papu''s Pyramid'; Arg = 'papupyramid' },
    @{ Name = 'Dragon Mines';    Arg = 'dragonmines' },
    @{ Name = 'Polar Pass';      Arg = 'polarpass' },
    @{ Name = 'Cortex Castle';   Arg = 'cortexcastle' },
    @{ Name = 'Tiny Arena';      Arg = 'tinyarena' },
    @{ Name = 'Hot Air Skyway';  Arg = 'hotairskyway' },
    @{ Name = 'N. Gin Labs';     Arg = 'nginlabs' },
    @{ Name = 'Oxide Station';   Arg = 'oxidestation' },
    @{ Name = 'Slide Coliseum';  Arg = 'slidecoliseum' },
    @{ Name = 'Turbo Track';     Arg = 'turbotrack' }
)

$Characters = @(
    @{ Name = 'Crash';      Arg = 'crash' },
    @{ Name = 'Cortex';     Arg = 'cortex' },
    @{ Name = 'Tiny';       Arg = 'tiny' },
    @{ Name = 'Coco';       Arg = 'coco' },
    @{ Name = 'N. Gin';     Arg = 'ngin' },
    @{ Name = 'Dingodile';  Arg = 'dingodile' },
    @{ Name = 'Polar';      Arg = 'polar' },
    @{ Name = 'Pura';       Arg = 'pura' },
    @{ Name = 'Pinstripe';  Arg = 'pinstripe' },
    @{ Name = 'Papu Papu';  Arg = 'papu' },
    @{ Name = 'Ripper Roo'; Arg = 'roo' },
    @{ Name = 'Komodo Joe'; Arg = 'joe' },
    @{ Name = 'N. Tropy';   Arg = 'ntropy' },
    @{ Name = 'Penta';      Arg = 'penta' },
    @{ Name = 'Fake Crash'; Arg = 'fakecrash' },
    @{ Name = 'Oxide';      Arg = 'oxide' }
)

$Options = @(
    @{ Name = 'Fullscreen';                         Arg = '--fullscreen' },
    @{ Name = 'Classic 4:3 picture';                Arg = '--4x3' },
    @{ Name = 'Retail kart models in multiplayer';  Arg = '--retail-karts' },
    @{ Name = 'Retail tracks in multiplayer';       Arg = '--retail-tracks' }
)

# ---------------------------------------------------------------------------
# Settings (last race and game options), kept in play-settings.json

function Read-Settings {
    $s = [pscustomobject]@{ Track = 'crashcove'; Character = 'crash'; Options = @() }
    if (Test-Path $SettingsFile) {
        try {
            $j = Get-Content $SettingsFile -Raw | ConvertFrom-Json
            if ($j.Track) { $s.Track = [string]$j.Track }
            if ($j.Character) { $s.Character = [string]$j.Character }
            if ($null -ne $j.Options) { $s.Options = @($j.Options | ForEach-Object { [string]$_ }) }
        } catch { }
    }
    return $s
}

function Save-Settings {
    $Settings | ConvertTo-Json | Set-Content -Path $SettingsFile -Encoding ASCII
}

function Find-Index($List, [string]$Arg) {
    for ($i = 0; $i -lt $List.Count; $i++) {
        if ($List[$i].Arg -eq $Arg) { return $i }
    }
    return 0
}

function Get-Name($List, [string]$Arg) {
    return $List[(Find-Index $List $Arg)].Name
}

# ---------------------------------------------------------------------------
# Menus

function Wait-Key([string]$Message = 'Press any key to continue') {
    Write-Host ''
    Write-Host "  $Message" -ForegroundColor DarkGray
    [void][Console]::ReadKey($true)
}

# Returns the picked index, or -1 for Esc.
function Select-Item([string]$Title, [string[]]$Items, [int]$Start = 0, [string[]]$Subtitle = @()) {
    $index = [Math]::Max(0, [Math]::Min($Start, $Items.Count - 1))
    while ($true) {
        Clear-Host
        Write-Host ''
        Write-Host "  $Title" -ForegroundColor Yellow
        foreach ($line in $Subtitle) { Write-Host "  $line" -ForegroundColor DarkGray }
        Write-Host ''
        for ($i = 0; $i -lt $Items.Count; $i++) {
            if ($i -eq $index) {
                Write-Host ("  > " + $Items[$i] + " ") -ForegroundColor Black -BackgroundColor Yellow
            } else {
                Write-Host ("    " + $Items[$i])
            }
        }
        Write-Host ''
        Write-Host '  Up/Down: move    Space/Enter: pick    Esc: back' -ForegroundColor DarkGray

        $key = [Console]::ReadKey($true)
        switch ($key.Key) {
            'UpArrow'   { $index = ($index - 1 + $Items.Count) % $Items.Count }
            'DownArrow' { $index = ($index + 1) % $Items.Count }
            'Home'      { $index = 0 }
            'End'       { $index = $Items.Count - 1 }
            'Spacebar'  { return $index }
            'Enter'     { return $index }
            'Escape'    { return -1 }
        }
    }
}

# ---------------------------------------------------------------------------
# Actions

function Invoke-UpdateAndBuild {
    Clear-Host
    Write-Host '=== git pull ===' -ForegroundColor Cyan
    & git pull --ff-only | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Wait-Key 'git pull failed (see above). Press any key to choose what to do.'
        $pick = Select-Item 'git pull failed: local changes, or no network' @('Build what I have anyway', 'Back to the menu')
        if ($pick -ne 0) { return $false }
    }

    Write-Host ''
    Write-Host '=== build ===' -ForegroundColor Cyan
    & cmd.exe /c build-msvc.bat | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host '  The build failed: see the messages above.' -ForegroundColor Red
        Wait-Key
        return $false
    }
    return $true
}

function Start-Game([string[]]$Extra = @()) {
    if (-not (Test-Path $Game)) {
        Clear-Host
        Write-Host ''
        Write-Host '  The game is not built yet: pick an option with "build" first.' -ForegroundColor Red
        Wait-Key
        return
    }

    $gameArgs = @($Settings.Options)
    if ($Extra) { $gameArgs += $Extra }

    Clear-Host
    Write-Host "  Starting the game $($gameArgs -join ' ')" -ForegroundColor Cyan
    & $Game @gameArgs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host '  The game exited with an error. The log is "Crash Team Racing.log".' -ForegroundColor Red
        Wait-Key
    }
}

function Start-Race {
    $track = Select-Item 'Pick a track' @($Tracks | ForEach-Object { $_.Name }) (Find-Index $Tracks $Settings.Track)
    if ($track -lt 0) { return }
    $character = Select-Item "Pick a character for $($Tracks[$track].Name)" @($Characters | ForEach-Object { $_.Name }) (Find-Index $Characters $Settings.Character)
    if ($character -lt 0) { return }

    $Settings.Track = $Tracks[$track].Arg
    $Settings.Character = $Characters[$character].Arg
    Save-Settings
    Start-Game @('--track', $Settings.Track, '--character', $Settings.Character)
}

function Edit-Options {
    $index = 0
    while ($true) {
        $items = @()
        foreach ($o in $Options) {
            $mark = if ($Settings.Options -contains $o.Arg) { '[x]' } else { '[ ]' }
            $items += "$mark $($o.Name)"
        }
        $items += 'Done'

        $index = Select-Item 'Game options (Space or Enter toggles)' $items $index
        if (($index -lt 0) -or ($index -eq $Options.Count)) {
            Save-Settings
            return
        }

        $arg = $Options[$index].Arg
        if ($Settings.Options -contains $arg) {
            $Settings.Options = @($Settings.Options | Where-Object { $_ -ne $arg })
        } else {
            $Settings.Options = @($Settings.Options) + $arg
        }
    }
}

# ---------------------------------------------------------------------------
# Main menu

$Settings = Read-Settings
$cursorWasVisible = $true
try { $cursorWasVisible = [Console]::CursorVisible; [Console]::CursorVisible = $false } catch { }

try {
    $menu = @(
        'Update + build + play',
        'Update + build + race',
        'Play',
        'Race',
        'Game options',
        'Quit'
    )
    $index = 0
    $running = $true
    while ($running) {
        $optionNames = @($Options | Where-Object { $Settings.Options -contains $_.Arg } | ForEach-Object { $_.Name })
        $subtitle = @(
            "Last race: $(Get-Name $Tracks $Settings.Track) as $(Get-Name $Characters $Settings.Character)",
            "Options:   $(if ($optionNames.Count -gt 0) { $optionNames -join ', ' } else { 'none' })"
        )
        $index = Select-Item 'SPESSU NIGHT' $menu $index $subtitle
        switch ($index) {
            0 { if (Invoke-UpdateAndBuild) { Start-Game } }
            1 { if (Invoke-UpdateAndBuild) { Start-Race } }
            2 { Start-Game }
            3 { Start-Race }
            4 { Edit-Options }
            default { $running = $false }
        }
        if ($index -lt 0) { $index = 0 }
    }
} finally {
    try { [Console]::CursorVisible = $cursorWasVisible } catch { }
    Clear-Host
}
