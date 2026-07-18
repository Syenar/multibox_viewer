# Creates a local test code-signing cert, catalogs + signs the driver, installs the package.
#Requires -RunAsAdministrator
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$stage = Join-Path $root "artifacts\$Configuration"
$work = Join-Path $env:TEMP "WindowDisplaySigned"
$log = Join-Path $env:TEMP "WindowDisplay-sign-install.log"
$certStoreName = "WindowDisplay Test Driver"

function Log($m) {
    $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m
    Add-Content $log $line
    Write-Host $line
}

Remove-Item $log -Force -ErrorAction SilentlyContinue
Log "Sign+install starting. Log=$log"

# Secure Boot status
try {
    $sb = Confirm-SecureBootUEFI
    Log "SecureBootUEFI=$sb"
} catch {
    Log "SecureBoot check failed: $($_.Exception.Message)"
    $sb = $null
}

$signtool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
$inf2cat = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe"
if (-not (Test-Path $signtool)) { throw "signtool missing" }
if (-not (Test-Path $inf2cat)) { throw "Inf2Cat missing" }

if (-not (Test-Path (Join-Path $stage "WindowDisplayDriver.dll"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $work | Out-Null
# Prefer source INF (stamped NTamd64) over staged copy
Copy-Item (Join-Path $stage "WindowDisplayDriver.dll") $work -Force
Copy-Item (Join-Path $root "src\driver\WindowDisplayDriver\WindowDisplayDriver.inf") $work -Force

$infPath = Join-Path $work "WindowDisplayDriver.inf"
$infText = Get-Content $infPath -Raw
$infText = $infText -replace 'NT\$ARCH\$', 'NTamd64'
$infText = $infText -replace '\$UMDFVERSION\$', '2.25.0'
if ($infText -notmatch "CatalogFile\s*=") {
    $infText = $infText -replace "DriverVer=", "CatalogFile=WindowDisplayDriver.cat`r`nDriverVer="
}
Set-Content -Path $infPath -Value $infText -Encoding ASCII

# Create or reuse code signing cert
$existing = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq "CN=$certStoreName" } | Select-Object -First 1
if (-not $existing) {
    Log "Creating self-signed code signing certificate..."
    $existing = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=$certStoreName" `
        -KeyUsage DigitalSignature `
        -FriendlyName $certStoreName `
        -CertStoreLocation "Cert:\LocalMachine\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") `
        -NotAfter (Get-Date).AddYears(5)
}
Log "Cert thumbprint=$($existing.Thumbprint)"

# Trust locally
$rootStore = "Cert:\LocalMachine\Root"
$pubStore = "Cert:\LocalMachine\TrustedPublisher"
foreach ($storePath in @($rootStore, $pubStore)) {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store(
        (Split-Path $storePath -Leaf), 'LocalMachine')
    $store.Open('ReadWrite')
    $store.Add($existing)
    $store.Close()
}
Log "Certificate installed to Root + TrustedPublisher"

# Export PFX outside package directory so Inf2Cat does not see it
$pfxDir = Join-Path $env:TEMP "WindowDisplayCerts"
New-Item -ItemType Directory -Force -Path $pfxDir | Out-Null
$pfx = Join-Path $pfxDir "WindowDisplayTest.pfx"
$pwdPlain = "WindowDisplay-Dev-Only"
$pwd = ConvertTo-SecureString -String $pwdPlain -Force -AsPlainText
Export-PfxCertificate -Cert $existing -FilePath $pfx -Password $pwd | Out-Null

# Inf2Cat (10_X64 = Windows 10/11 x64)
Log "Running Inf2Cat..."
& $inf2cat /driver:$work /os:10_X64 /verbose 2>&1 | ForEach-Object { Log $_ }
$cat = Join-Path $work "WindowDisplayDriver.cat"
if (-not (Test-Path $cat)) {
    # Inf2Cat names cat from CatalogFile=
    $cat = Get-ChildItem $work -Filter "*.cat" | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cat -or -not (Test-Path $cat)) { throw "Catalog (.cat) was not produced" }
Log "Catalog: $cat"

# Sign DLL then CAT
Log "Signing DLL + CAT..."
& $signtool sign /fd SHA256 /f $pfx /p $pwdPlain /tr http://timestamp.digicert.com /td SHA256 (Join-Path $work "WindowDisplayDriver.dll") 2>&1 | ForEach-Object { Log $_ }
& $signtool sign /fd SHA256 /f $pfx /p $pwdPlain /tr http://timestamp.digicert.com /td SHA256 $cat 2>&1 | ForEach-Object { Log $_ }
& $signtool verify /pa /v $cat 2>&1 | Select-Object -Last 20 | ForEach-Object { Log $_ }

# Copy signed package back to artifacts
Copy-Item (Join-Path $work "*") $stage -Force
Log "Signed files copied to $stage"

# Enable testsigning if possible
Log "Attempting bcdedit testsigning on..."
$ts = & bcdedit /set testsigning on 2>&1 | Out-String
Log $ts.Trim()

# Install
Log "pnputil install..."
$pnp = & pnputil /add-driver (Join-Path $work "WindowDisplayDriver.inf") /install 2>&1 | Out-String
Log $pnp

$enum = & pnputil /enum-drivers 2>&1 | Out-String
if ($enum -match "WindowDisplayDriver") {
    Log "SUCCESS: WindowDisplayDriver is present in driver store"
} else {
    Log "Driver not listed in store yet."
    if ($sb -eq $true) {
        Log "BLOCKER: Secure Boot is ON. Disable Secure Boot in UEFI firmware, reboot, then re-run this script (testsigning cannot be enabled while Secure Boot is on)."
    } else {
        Log "If install failed, reboot after testsigning enable and retry."
    }
}

Log "Done."
