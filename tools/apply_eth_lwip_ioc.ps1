param(
  [string]$IocPath = "greenhouseOS.ioc"
)

if (-not (Test-Path $IocPath)) {
  throw "IOC file not found: $IocPath"
}

$backup = "$IocPath.bak_eth_lwip"
Copy-Item $IocPath $backup -Force

$lines = Get-Content $IocPath
$map = [ordered]@{}
$order = New-Object System.Collections.Generic.List[string]
$raw = New-Object System.Collections.Generic.List[string]

foreach ($l in $lines) {
  if ($l.StartsWith("#") -or -not ($l -match "=")) {
    $raw.Add($l)
    continue
  }
  $idx = $l.IndexOf("=")
  $k = $l.Substring(0, $idx)
  $v = $l.Substring($idx + 1)
  if (-not $map.Contains($k)) {
    $order.Add($k) | Out-Null
  }
  $map[$k] = $v
}

function Set-Key([string]$k, [string]$v) {
  if (-not $map.Contains($k)) {
    $order.Add($k) | Out-Null
  }
  $map[$k] = $v
}

# Add ETH/LWIP IP blocks
$ips = @()
foreach ($k in $order) {
  if ($k -match '^Mcu\.IP\d+$') { $ips += $map[$k] }
}
if ($ips -notcontains 'ETH') { $ips += 'ETH' }
if ($ips -notcontains 'LWIP') { $ips += 'LWIP' }

# remove old Mcu.IPx keys from order/map
$ipKeys = @($order | Where-Object { $_ -match '^Mcu\.IP\d+$' })
foreach ($k in $ipKeys) {
  $order.Remove($k) | Out-Null
  $map.Remove($k)
}

for ($i=0; $i -lt $ips.Count; $i++) {
  Set-Key "Mcu.IP$i" $ips[$i]
}
Set-Key "Mcu.IPNb" ([string]$ips.Count)

# Add RMII pins and virtual pins
$pins = @()
foreach ($k in $order) {
  if ($k -match '^Mcu\.Pin\d+$') { $pins += $map[$k] }
}
$requiredPins = @(
  'PA1','PA2','PA7','PB11','PB12','PB13','PC1','PC4','PC5',
  'VP_ETH_VS_ETH','VP_LWIP_VS_LWIP'
)
foreach ($p in $requiredPins) {
  if ($pins -notcontains $p) { $pins += $p }
}

$pinKeys = @($order | Where-Object { $_ -match '^Mcu\.Pin\d+$' })
foreach ($k in $pinKeys) {
  $order.Remove($k) | Out-Null
  $map.Remove($k)
}
for ($i=0; $i -lt $pins.Count; $i++) {
  Set-Key "Mcu.Pin$i" $pins[$i]
}
Set-Key "Mcu.PinsNb" ([string]$pins.Count)

# Pin functions
Set-Key 'PA1.Signal' 'ETH_RMII_REF_CLK'
Set-Key 'PA2.Signal' 'ETH_MDIO'
Set-Key 'PA7.Signal' 'ETH_RMII_CRS_DV'
Set-Key 'PB11.Signal' 'ETH_RMII_TX_EN'
Set-Key 'PB12.Signal' 'ETH_RMII_TXD0'
Set-Key 'PB13.Signal' 'ETH_RMII_TXD1'
Set-Key 'PC1.Signal' 'ETH_MDC'
Set-Key 'PC4.Signal' 'ETH_RMII_RXD0'
Set-Key 'PC5.Signal' 'ETH_RMII_RXD1'

# ETH config
Set-Key 'ETH.Mode' 'RMII'
Set-Key 'ETH.IPParameters' 'MediaInterface,AutoNegotiation,PhyAddress'
Set-Key 'ETH.MediaInterface' 'RMII'
Set-Key 'ETH.AutoNegotiation' 'ETH_AUTONEGOTIATION_ENABLE'
Set-Key 'ETH.PhyAddress' '1'

# LwIP config (static)
Set-Key 'LWIP.Mode' 'RTOS'
Set-Key 'LWIP.IPParameters' 'LWIP_DHCP,LWIP_NETMASK,LWIP_GATEWAY,LWIP_IPADDR,LWIP_NETCONN,LWIP_SOCKET,MEM_SIZE,PBUF_POOL_SIZE,MEMP_NUM_TCP_PCB,MEMP_NUM_TCP_SEG,TCP_MSS,TCP_WND,TCP_SND_BUF'
Set-Key 'LWIP.LWIP_DHCP' 'Disabled'
Set-Key 'LWIP.LWIP_IPADDR' '192.168.50.20'
Set-Key 'LWIP.LWIP_NETMASK' '255.255.255.0'
Set-Key 'LWIP.LWIP_GATEWAY' '192.168.50.1'
Set-Key 'LWIP.LWIP_NETCONN' 'Enabled'
Set-Key 'LWIP.LWIP_SOCKET' 'Disabled'
Set-Key 'LWIP.MEM_SIZE' '16384'
Set-Key 'LWIP.PBUF_POOL_SIZE' '24'
Set-Key 'LWIP.MEMP_NUM_TCP_PCB' '6'
Set-Key 'LWIP.MEMP_NUM_TCP_SEG' '32'
Set-Key 'LWIP.TCP_MSS' '1460'
Set-Key 'LWIP.TCP_WND' '5840'
Set-Key 'LWIP.TCP_SND_BUF' '5840'

Set-Key 'VP_ETH_VS_ETH.Mode' 'RMII'
Set-Key 'VP_ETH_VS_ETH.Signal' 'ETH_VS_ETH'
Set-Key 'VP_LWIP_VS_LWIP.Mode' 'Enabled'
Set-Key 'VP_LWIP_VS_LWIP.Signal' 'LWIP_VS_LWIP'

# Increase heap target to avoid LwIP starvation in generated project settings
Set-Key 'ProjectManager.HeapSize' '0x14000'

# Rebuild output preserving comment/non key lines at top
$out = New-Object System.Collections.Generic.List[string]
foreach ($l in $raw) {
  if ($l.StartsWith('#')) { $out.Add($l) | Out-Null }
}

foreach ($k in $order) {
  $out.Add("$k=$($map[$k])") | Out-Null
}

Set-Content -Path $IocPath -Value $out -Encoding ASCII
Write-Host "Patched: $IocPath"
Write-Host "Backup:  $backup"
