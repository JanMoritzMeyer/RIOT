#!/bin/bash
# mount_nrf52840.sh - Automatisches Mounten des nRF52840 Feather Sense

echo "🔍 Suche nRF52840 Feather Sense..."

# Suche nach dem UF2-Bootloader Device
UF2_DEVICE=$(lsblk -rno NAME,LABEL | grep FTHRSNSBOOT | awk '{print $1}')

if [ -z "$UF2_DEVICE" ]; then
    echo "❌ Kein nRF52840 Board im UF2-Bootloader-Modus gefunden!"
    echo ""
    echo "📝 Lösungsschritte:"
    echo "1. Reset-Button am Board 2x schnell drücken"
    echo "2. Board sollte als 'FTHRSNSBOOT' erscheinen"
    echo "3. Script erneut ausführen"
    exit 1
fi

echo "✅ Board gefunden: /dev/$UF2_DEVICE"

# Mount-Point erstellen
sudo mkdir -p /mnt/feather

# Prüfen ob bereits gemountet
if mountpoint -q /mnt/feather; then
    echo "⚠️  /mnt/feather ist bereits gemountet, unmounte zuerst..."
    sudo umount /mnt/feather
fi

# Board mit korrekten Permissions mounten
echo "🔧 Mounte Board mit Benutzer-Permissions..."
sudo mount -o uid=$(id -u),gid=$(id -g),umask=0002 /dev/$UF2_DEVICE /mnt/feather

if [ $? -eq 0 ]; then
    echo "✅ Board erfolgreich gemountet unter /mnt/feather"
    echo "📁 Inhalt:"
    ls -la /mnt/feather/
    echo ""
    echo "🚀 Bereit zum Flashen!"
else
    echo "❌ Mount fehlgeschlagen!"
    exit 1
fi
