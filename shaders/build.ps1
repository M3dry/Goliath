$OUT = if ($args[0]) { $args[0] } else { "./build-win/" }
if (!(Test-Path $OUT)) { New-Item -ItemType Directory -Path $OUT -Force }

Write-Host "OUT directory: $OUT" -ForegroundColor Cyan

function Compile-Shaders($Stage, $SubDir) {
    # Resolve the absolute path of the subdir to calculate relative paths manually
    $BaseDir = (Get-Item "./shaders/$SubDir").FullName
    $Files = Get-ChildItem "$BaseDir" -Recurse -Filter *.glsl

    foreach ($File in $Files) {
        # Manual relative path calculation for PowerShell 5.1
        $RelPath = $File.FullName.Replace("$BaseDir\", "")
        
        # Flatten name: replace path separators with underscores and change extension
        $FlattenedName = ($RelPath -replace '[\\/]', '_') -replace '\.glsl$', '.spv'
        
        Write-Host "Compiling $FlattenedName , stage: $Stage"
        
        # Use -- to stop parameter parsing and pass variables properly
        & glslc -g -I"./shaders" "-fshader-stage=$Stage" "$($File.FullName)" -o "$OUT/$FlattenedName"
    }
}

Compile-Shaders "compute" "compute"
Compile-Shaders "vertex" "vertex"
Compile-Shaders "fragment" "fragment"
