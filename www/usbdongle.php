<?php
require_once("common.php");

echo "<!--\n";
$usbDonglePort = ReadSettingFromFile("USBDonglePort");
if (($usbDonglePort == false) || ($usbDonglePort == ""))
	$usbDonglePort = "DISABLED";
$usbDongleType = ReadSettingFromFile("USBDongleType");
if ($usbDongleType == false)
	$usbDongleType = "DMX";
echo "-->\n";

function PrintUSBSerialPortOptions()
{
	global $usbDonglePort;

	echo "<option value='DISABLED' ";
	if ($usbDonglePort == "DISABLED")
		echo "SELECTED";
	echo ">-- DISABLED --</option>\n";

	foreach(scandir("/dev/") as $devFile)
	{
		if (preg_match('/^ttyUSB.*/', $devFile))
		{
			echo "<option value='$devFile' ";
			if ($devFile == $usbDonglePort)
				echo "SELECTED";
			echo ">$devFile</option>\n";
		}
	}
}

?>
<!DOCTYPE html>
<html>
<head>
<?php include 'common/menuHead.inc'; ?>
<script type="text/javascript" src="/js/fpp.js"></script>
<title>Falcon PI Player - FPP</title>
</head>
<body>
<div id="bodyWrapper">
  <?php include 'menu.inc'; ?>
  <br/>


<div id="time" class="settings">
<fieldset>
<legend>USB Dongle Setup</legend>
<table>
<tr><td>USB Dongle:</td><td><select id='USBDonglePort'>
<?
	PrintUSBSerialPortOptions();
?>
</select></td></tr>
<tr><td>Dongle Type*:</td><td>
	<select id='USBDongleType'>
		<option value='DMX' <? if ($usbDongleType == "DMX") echo "SELECTED"; ?>>DMX</option>
		<option value='Pixelnet' <? if ($usbDongleType == "Pixelnet") echo "SELECTED"; ?>>Pixelnet</option>
	</select>
</td></tr>
</table>
<br>
<input type=submit value='Submit' onClick='SaveUSBDongleSettings();'>

<hr>
<font size=-1>
* The DMX dongle support should be compatible with Entec Pro, Lynx DMX,
DIYC RPM, DMXking.com, and DIYblinky.com dongles using a universe size of
up to 512 channels.  The Pixelnet dongle support is compatible with the
Lynx Pixelnet USB dongle using up to 4096 channels.  Currently only one
dongle is supported at a time and FPP always outputs the first universe
in the channel data.</font>
</fieldset>
</div>


</div>
<?php	include 'common/footer.inc'; ?>
</body>
</html>
