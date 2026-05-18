$ErrorActionPreference = 'Stop'

$Root = (Get-Location).Path
$PayloadText = if ($MyInvocation.ExpectingInput) {
    $input | Out-String
} elseif ([Console]::IsInputRedirected) {
    [Console]::In.ReadToEnd()
} else {
    ''
}
$CandidatePaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$CppExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$WriteToolRegex = [regex]'(?i)(apply[_-]?patch|create[_-]?file|edit[_-]?file|write[_-]?file|insert[_-]?edit|replace[_-]?string|str[_-]?replace|update[_-]?file)'
$CppExtensionRegex = '(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp|cu|cuh)'

foreach ($extension in @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl', '.ipp', '.cu', '.cuh')) {
    [void]$CppExtensions.Add($extension)
}

function Write-HookResponse {
    param(
        [string]$Message = ''
    )

    $response = @{ 'continue' = $true }
    if (-not [string]::IsNullOrWhiteSpace($Message)) {
        $response.systemMessage = $Message
    }

    [Console]::Out.WriteLine(($response | ConvertTo-Json -Compress))
}

function Test-ScalarNode {
    param([object]$Node)

    if ($null -eq $Node) {
        return $true
    }

    if ($Node -is [string]) {
        return $true
    }

    $nodeType = $Node.GetType()
    return $nodeType.IsPrimitive -or $Node -is [decimal] -or $Node -is [datetime] -or $Node -is [guid]
}

function Test-WriteToolText {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $false
    }

    return $WriteToolRegex.IsMatch($Text)
}

function Test-WriteToolPayload {
    param(
        [object]$Node,
        [int]$Depth = 0
    )

    if ($null -eq $Node -or $Depth -gt 40) {
        return $false
    }

    if (Test-ScalarNode $Node) {
        return $false
    }

    if ($Node -is [System.Collections.IDictionary]) {
        foreach ($key in $Node.Keys) {
            $value = $Node[$key]
            if ($key -match '(?i)(tool|name|command|recipient|operation|method|editType)' -and (Test-WriteToolText ([string]$value))) {
                return $true
            }

            if (Test-WriteToolPayload $value ($Depth + 1)) {
                return $true
            }
        }

        return $false
    }

    if ($Node -is [System.Collections.IEnumerable] -and $Node -isnot [string]) {
        foreach ($child in $Node) {
            if (Test-WriteToolPayload $child ($Depth + 1)) {
                return $true
            }
        }

        return $false
    }

    foreach ($property in $Node.PSObject.Properties) {
        if ($property.Name -match '(?i)(tool|name|command|recipient|operation|method|editType)' -and (Test-WriteToolText ([string]$property.Value))) {
            return $true
        }

        if (Test-WriteToolPayload $property.Value ($Depth + 1)) {
            return $true
        }
    }

    return $false
}

function Add-CandidatePath {
    param([string]$RawPath)

    if ([string]::IsNullOrWhiteSpace($RawPath)) {
        return
    }

    $candidatePath = $RawPath.Trim().Trim('"', "'")
    if ([string]::IsNullOrWhiteSpace($candidatePath)) {
        return
    }

    if ($candidatePath.StartsWith('file:', [System.StringComparison]::OrdinalIgnoreCase)) {
        try {
            $candidatePath = ([System.Uri]$candidatePath).LocalPath
        } catch {
            return
        }
    }

    try {
        if (-not [System.IO.Path]::IsPathRooted($candidatePath)) {
            $candidatePath = Join-Path -Path $Root -ChildPath $candidatePath
        }

        $fullPath = [System.IO.Path]::GetFullPath($candidatePath)
        $fullRoot = [System.IO.Path]::GetFullPath($Root)
        $extension = [System.IO.Path]::GetExtension($fullPath)

        if (-not $CppExtensions.Contains($extension)) {
            return
        }

        if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            return
        }

        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            return
        }

        [void]$CandidatePaths.Add($fullPath)
    } catch {
    }
}

function Add-PathsFromText {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return
    }

    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\*\*\*\s+(?:Add|Update|Delete)\s+File:\s+(.+)$') {
            Add-CandidatePath $Matches[1]
        }

        if ($line -match "^[+-]{3}\s+[ab]/(.+?\.($CppExtensionRegex))(?:\s|$)") {
            Add-CandidatePath $Matches[1]
        }
    }

    $windowsMatches = [regex]::Matches($Text, "(?i)([A-Za-z]:\\[^`"'<>|?*`r`n]+?\.($CppExtensionRegex))")
    foreach ($matchInfo in $windowsMatches) {
        Add-CandidatePath $matchInfo.Groups[1].Value
    }

    $fileUriMatches = [regex]::Matches($Text, "(?i)(file://[^`"'<>|?*`r`n]+?\.($CppExtensionRegex))")
    foreach ($matchInfo in $fileUriMatches) {
        Add-CandidatePath $matchInfo.Groups[1].Value
    }
}

function Visit-Paths {
    param(
        [object]$Node,
        [int]$Depth = 0
    )

    if ($null -eq $Node -or $Depth -gt 40) {
        return
    }

    if ($Node -is [string]) {
        Add-CandidatePath $Node
        Add-PathsFromText $Node
        return
    }

    if (Test-ScalarNode $Node) {
        return
    }

    if ($Node -is [System.Collections.IDictionary]) {
        foreach ($key in $Node.Keys) {
            Visit-Paths $Node[$key] ($Depth + 1)
        }

        return
    }

    if ($Node -is [System.Collections.IEnumerable] -and $Node -isnot [string]) {
        foreach ($child in $Node) {
            Visit-Paths $child ($Depth + 1)
        }

        return
    }

    foreach ($property in $Node.PSObject.Properties) {
        Visit-Paths $property.Value ($Depth + 1)
    }
}

if ([string]::IsNullOrWhiteSpace($PayloadText)) {
    Write-HookResponse
    exit 0
}

try {
    $Payload = $PayloadText | ConvertFrom-Json -Depth 100
} catch {
    Write-HookResponse
    exit 0
}

if (-not (Test-WriteToolPayload $Payload)) {
    Write-HookResponse
    exit 0
}

Visit-Paths $Payload

if ($CandidatePaths.Count -eq 0) {
    Write-HookResponse
    exit 0
}

$clangFormat = $env:CLANG_FORMAT
if ([string]::IsNullOrWhiteSpace($clangFormat)) {
    $commandInfo = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($null -ne $commandInfo) {
        $clangFormat = $commandInfo.Source
    }
}

if ([string]::IsNullOrWhiteSpace($clangFormat)) {
    Write-HookResponse 'Copilot Clang Format skipped: clang-format was not found on PATH. Set CLANG_FORMAT or update PATH.'
    exit 0
}

$formattedPaths = [System.Collections.Generic.List[string]]::new()
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($sourcePath in $CandidatePaths) {
    $formatOutput = & $clangFormat -i --style=file --fallback-style=LLVM $sourcePath 2>&1
    if ($LASTEXITCODE -eq 0) {
        [void]$formattedPaths.Add($sourcePath)
        continue
    }

    $failureText = ($formatOutput | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($failureText)) {
        $failureText = "exit code $LASTEXITCODE"
    }

    [void]$failures.Add("$sourcePath ($failureText)")
}

if ($failures.Count -gt 0) {
    Write-HookResponse "Copilot Clang Format failed for $($failures.Count) file(s): $($failures -join '; ')"
    exit 0
}

Write-HookResponse "Copilot Clang Format applied clang-format to $($formattedPaths.Count) C/C++ file(s)."