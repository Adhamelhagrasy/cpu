param(
    [int]$Port = 8080
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $projectRoot "os_sim.exe"
$sessions = @{}
$promptPattern = '^\[PID \d+\] input for [^:\r\n]+:$'

function Write-Log {
    param([string]$Message)
    Write-Host ("[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $Message)
}

function Get-ContentType {
    param([string]$Path)

    $ext = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    switch ($ext) {
        ".html" { return "text/html; charset=utf-8" }
        ".css" { return "text/css; charset=utf-8" }
        ".js" { return "application/javascript; charset=utf-8" }
        default { return "application/octet-stream" }
    }
}

function Write-HttpResponse {
    param(
        [Parameter(Mandatory = $true)][System.Net.Sockets.NetworkStream]$Stream,
        [int]$StatusCode = 200,
        [string]$StatusText = "OK",
        [byte[]]$Body = @(),
        [string]$ContentType = "text/plain; charset=utf-8"
    )

    $header = "HTTP/1.1 $StatusCode $StatusText`r`nContent-Type: $ContentType`r`nContent-Length: $($Body.Length)`r`nConnection: close`r`n`r`n"
    $headerBytes = [System.Text.Encoding]::ASCII.GetBytes($header)
    $Stream.Write($headerBytes, 0, $headerBytes.Length)
    if ($Body.Length -gt 0) {
        $Stream.Write($Body, 0, $Body.Length)
    }
    $Stream.Flush()
}

function Write-TextResponse {
    param(
        [Parameter(Mandatory = $true)][System.Net.Sockets.NetworkStream]$Stream,
        [string]$Text,
        [int]$StatusCode = 200,
        [string]$StatusText = "OK",
        [string]$ContentType = "text/plain; charset=utf-8"
    )

    $body = [System.Text.Encoding]::UTF8.GetBytes($Text)
    Write-HttpResponse -Stream $Stream -StatusCode $StatusCode -StatusText $StatusText -Body $body -ContentType $ContentType
}

function Write-JsonResponse {
    param(
        [Parameter(Mandatory = $true)][System.Net.Sockets.NetworkStream]$Stream,
        [Parameter(Mandatory = $true)]$Data,
        [int]$StatusCode = 200,
        [string]$StatusText = "OK"
    )

    $json = $Data | ConvertTo-Json -Depth 8
    Write-TextResponse -Stream $Stream -Text $json -StatusCode $StatusCode -StatusText $StatusText -ContentType "application/json; charset=utf-8"
}

function Read-HttpRequest {
    param([Parameter(Mandatory = $true)][System.Net.Sockets.NetworkStream]$Stream)

    $reader = New-Object System.IO.StreamReader($Stream, [System.Text.Encoding]::ASCII, $false, 4096, $true)
    $requestLine = $reader.ReadLine()
    if ([string]::IsNullOrWhiteSpace($requestLine)) {
        return $null
    }

    $parts = $requestLine.Split(' ')
    if ($parts.Count -lt 2) {
        throw "Malformed request line."
    }

    $headers = @{}
    while ($true) {
        $line = $reader.ReadLine()
        if ($line -eq $null -or $line -eq "") {
            break
        }
        $idx = $line.IndexOf(':')
        if ($idx -gt 0) {
            $name = $line.Substring(0, $idx).Trim().ToLowerInvariant()
            $value = $line.Substring($idx + 1).Trim()
            $headers[$name] = $value
        }
    }

    $body = ""
    if ($headers.ContainsKey("content-length")) {
        $length = [int]$headers["content-length"]
        if ($length -gt 0) {
            $buffer = New-Object char[] $length
            $offset = 0
            while ($offset -lt $length) {
                $read = $reader.Read($buffer, $offset, $length - $offset)
                if ($read -le 0) {
                    break
                }
                $offset += $read
            }
            $body = -join $buffer[0..($offset - 1)]
        }
    }

    return @{
        Method = $parts[0].ToUpperInvariant()
        Path = $parts[1]
        Headers = $headers
        Body = $body
    }
}

function Send-StaticFile {
    param(
        [Parameter(Mandatory = $true)][System.Net.Sockets.NetworkStream]$Stream,
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-TextResponse -Stream $Stream -Text "Not found" -StatusCode 404 -StatusText "Not Found"
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    Write-HttpResponse -Stream $Stream -StatusCode 200 -StatusText "OK" -Body $bytes -ContentType (Get-ContentType -Path $Path)
}

function New-StartupInput {
    param([Parameter(Mandatory = $true)]$Payload)

    $lines = New-Object System.Collections.Generic.List[string]
    $scheduler = if ($Payload.scheduler) { [string]$Payload.scheduler } else { "RR" }
    $lines.Add($scheduler)

    if ($scheduler -eq "RR") {
        $quantum = if ([int]$Payload.quantum -gt 0) { [int]$Payload.quantum } else { 2 }
        $lines.Add([string]$quantum)
    }

    $programs = @($Payload.programs)
    $lines.Add([string]$programs.Count)

    foreach ($program in $programs) {
        $lines.Add([string]$program.filename)
        $lines.Add([string]([int]$program.arrivalTime))
    }

    return (($lines -join "`n") + "`n")
}

function Append-SessionLines {
    param([Parameter(Mandatory = $true)]$Session)

    $idleCycles = 0

    while ($true) {
        $progress = $false
        while ($Session.ErrorReadIndex -lt $Session.ErrorLines.Count) {
            [void]$Session.Lines.Add([string]$Session.ErrorLines[$Session.ErrorReadIndex])
            $Session.ErrorReadIndex++
        }
        while ($Session.ReadIndex -lt $Session.Lines.Count) {
            $line = [string]$Session.Lines[$Session.ReadIndex]
            $Session.ReadIndex++
            if ($Session.Output.Length -gt 0) {
                $Session.Output += "`r`n"
            }
            $Session.Output += $line
            $Session.LastLine = $line
            $progress = $true
        }

        if ($progress) {
            if ($Session.LastLine -match $promptPattern) {
                break
            }
            $idleCycles = 0
            Start-Sleep -Milliseconds 40
            continue
        }

        if ($Session.Process.HasExited) {
            break
        }

        $idleCycles++
        if ($idleCycles -ge 10) {
            break
        }
        Start-Sleep -Milliseconds 40
    }

    return $Session.Output
}

function Build-SessionResponse {
    param(
        [string]$SessionId,
        $Session
    )

    $prompt = if ($Session.LastLine -match $promptPattern) { $Session.LastLine } else { $null }
    $completed = $Session.Process.HasExited
    $summary = "Scheduler: {0} | Programs: {1}" -f $Session.Scheduler, $Session.ProgramCount
    if ($completed) {
        $summary += " | Exit code: $($Session.Process.ExitCode)"
    }

    return @{
        ok = ($completed -eq $false -or $Session.Process.ExitCode -eq 0)
        sessionId = $SessionId
        summary = $summary
        output = $Session.Output
        waitingForInput = ($null -ne $prompt)
        prompt = $prompt
        completed = $completed
    }
}

function Start-SimulatorSession {
    param([Parameter(Mandatory = $true)]$Payload)

    if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
        throw "Missing executable at $exePath. Run run.bat to build it first."
    }

    $inputText = New-StartupInput -Payload $Payload
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath
    $psi.WorkingDirectory = $projectRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $null = $process.Start()
    $stdoutLines = [System.Collections.ArrayList]::Synchronized((New-Object System.Collections.ArrayList))
    $stderrLines = [System.Collections.ArrayList]::Synchronized((New-Object System.Collections.ArrayList))

    $stdoutEvent = Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -MessageData $stdoutLines -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$Event.MessageData.Add($EventArgs.Data)
        }
    }
    $stderrEvent = Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -MessageData $stderrLines -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$Event.MessageData.Add("[stderr] $($EventArgs.Data)")
        }
    }

    $process.BeginOutputReadLine()
    $process.BeginErrorReadLine()
    $process.StandardInput.Write($inputText)

    $sessionId = [guid]::NewGuid().ToString()
    $session = @{
        Process = $process
        Output = ""
        Lines = $stdoutLines
        ErrorLines = $stderrLines
        ReadIndex = 0
        ErrorReadIndex = 0
        LastLine = ""
        Scheduler = [string]$Payload.scheduler
        ProgramCount = @($Payload.programs).Count
        EventNames = @($stdoutEvent.Name, $stderrEvent.Name)
    }
    Append-SessionLines -Session $session | Out-Null
    $sessions[$sessionId] = $session

    if ($session.Process.HasExited) {
        $session.Process.StandardInput.Close()
    }

    return @{
        SessionId = $sessionId
        Session = $session
    }
}

function Submit-SimulatorInput {
    param(
        [Parameter(Mandatory = $true)][string]$SessionId,
        [Parameter(Mandatory = $true)][string]$InputText
    )

    if (-not $sessions.ContainsKey($SessionId)) {
        throw "Session not found."
    }

    $session = $sessions[$SessionId]
    if ($session.Process.HasExited) {
        return $session
    }

    $session.Process.StandardInput.WriteLine($InputText)
    Append-SessionLines -Session $session | Out-Null

    if ($session.Process.HasExited) {
        $session.Process.StandardInput.Close()
    }

    return $session
}

function Close-SimulatorSession {
    param([string]$SessionId)

    if (-not $sessions.ContainsKey($SessionId)) {
        return
    }

    $session = $sessions[$SessionId]
    foreach ($eventName in $session.EventNames) {
        Unregister-Event -SourceIdentifier $eventName -ErrorAction SilentlyContinue
    }
    $sessions.Remove($SessionId)
}

$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()

Write-Log "OS5 web UI available at http://localhost:$Port/"

try {
    while ($true) {
        $client = $listener.AcceptTcpClient()
        $stream = $client.GetStream()

        try {
            $request = Read-HttpRequest -Stream $stream
            if ($null -eq $request) {
                continue
            }

            $path = ($request.Path.Split('?')[0])

            if ($request.Method -eq "GET" -and $path -eq "/") {
                Send-StaticFile -Stream $stream -Path (Join-Path $PSScriptRoot "index.html")
                continue
            }

            if ($request.Method -eq "GET" -and $path -eq "/styles.css") {
                Send-StaticFile -Stream $stream -Path (Join-Path $PSScriptRoot "styles.css")
                continue
            }

            if ($request.Method -eq "GET" -and $path -eq "/app.js") {
                Send-StaticFile -Stream $stream -Path (Join-Path $PSScriptRoot "app.js")
                continue
            }

            if ($request.Method -eq "POST" -and $path -eq "/api/start") {
                $payload = $request.Body | ConvertFrom-Json
                if (-not $payload.programs -or @($payload.programs).Count -lt 1) {
                    Write-JsonResponse -Stream $stream -Data @{ ok = $false; error = "At least one program is required." } -StatusCode 400 -StatusText "Bad Request"
                    continue
                }

                $started = Start-SimulatorSession -Payload $payload
                $response = Build-SessionResponse -SessionId $started.SessionId -Session $started.Session
                if ($started.Session.Process.HasExited) {
                    Close-SimulatorSession -SessionId $started.SessionId
                }
                $statusCode = if ($response.ok) { 200 } else { 500 }
                $statusText = if ($response.ok) { "OK" } else { "Internal Server Error" }
                Write-JsonResponse -Stream $stream -Data $response -StatusCode $statusCode -StatusText $statusText
                continue
            }

            if ($request.Method -eq "POST" -and $path -eq "/api/input") {
                $payload = $request.Body | ConvertFrom-Json
                if (-not $payload.sessionId) {
                    Write-JsonResponse -Stream $stream -Data @{ ok = $false; error = "sessionId is required." } -StatusCode 400 -StatusText "Bad Request"
                    continue
                }

                $session = Submit-SimulatorInput -SessionId ([string]$payload.sessionId) -InputText ([string]$payload.input)
                $response = Build-SessionResponse -SessionId ([string]$payload.sessionId) -Session $session
                if ($session.Process.HasExited) {
                    Close-SimulatorSession -SessionId ([string]$payload.sessionId)
                }
                $statusCode = if ($response.ok) { 200 } else { 500 }
                $statusText = if ($response.ok) { "OK" } else { "Internal Server Error" }
                Write-JsonResponse -Stream $stream -Data $response -StatusCode $statusCode -StatusText $statusText
                continue
            }

            Write-TextResponse -Stream $stream -Text "Not found" -StatusCode 404 -StatusText "Not Found"
        } catch {
            Write-JsonResponse -Stream $stream -Data @{
                ok = $false
                error = $_.Exception.Message
            } -StatusCode 500 -StatusText "Internal Server Error"
        } finally {
            $stream.Close()
            $client.Close()
        }
    }
} finally {
    $listener.Stop()
}
