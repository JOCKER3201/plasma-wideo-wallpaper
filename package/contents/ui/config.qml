/*
    SPDX-FileCopyrightText: 2026 JOCKER3201 <58137709+JOCKER3201@users.noreply.github.com>

    SPDX-License-Identifier: GPL-2.0-or-later

    UI konfiguracji tapety "HDR Video" (io.github.jocker3201.hdrvideo).

    Konwencja cfg_: dialog konfiguracji Plasmy (ConfigurationContainmentAppearance.qml)
    przy otwarciu wstrzykuje wartości kluczy z contents/config/main.xml do właściwości
    cfg_<Klucz> jako initial properties, a przy OK/Zastosuj zapisuje je z powrotem do
    wallpaperConfiguration. Typy właściwości MUSZĄ zgadzać się z typami kcfg.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: root

    // Wyrównanie kolumn z pozostałymi FormLayoutami dialogu; "parentLayout" to
    // właściwość przodka o id "appearanceRoot" (kontekst dialogu), jak w org.kde.color.
    twinFormLayouts: parentLayout
    property alias formLayout: root

    // Wstrzykiwane przez dialog jako initial properties (deklaracja wycisza ostrzeżenia).
    property var configDialog
    property var wallpaperConfiguration

    // Klucze z main.xml, grupa General — typy zgodne z kcfg.
    property string cfg_VideoFile   // String: ścieżka/URL pliku wideo
    property int cfg_HdrMode        // Int: 0=auto, 1=wymuś HDR, 2=SDR (off)
    property int cfg_Peak           // Int: szczytowa luminancja w nitach
    property bool cfg_Sdr2Hdr       // Bool: inverse tone mapping SDR→HDR
    property bool cfg_Contain       // Bool: dopasowanie z pasami zamiast kadrowania

    // Pseudo-klucze <Klucz>Default z KConfigPropertyMap (wzorzec org.kde.image);
    // dirty-check dialogu pomija właściwości kończące się na "Default".
    property string cfg_VideoFileDefault
    property int cfg_HdrModeDefault
    property int cfg_PeakDefault
    property bool cfg_Sdr2HdrDefault
    property bool cfg_ContainDefault

    // Re-synchronizacja kontrolek, gdy cfg_* zmieni się poza nimi
    // (np. przycisk "Przywróć domyślne" w dialogu).
    onCfg_VideoFileChanged: if (videoFileField.text !== cfg_VideoFile) {
        videoFileField.text = cfg_VideoFile;
    }
    onCfg_HdrModeChanged: hdrModeComboBox.currentIndex = hdrModeComboBox.indexOfValue(cfg_HdrMode)
    onCfg_PeakChanged: if (peakSpinBox.value !== cfg_Peak) {
        peakSpinBox.value = cfg_Peak;
    }
    onCfg_Sdr2HdrChanged: if (sdr2hdrCheckBox.checked !== cfg_Sdr2Hdr) {
        sdr2hdrCheckBox.checked = cfg_Sdr2Hdr;
    }
    onCfg_ContainChanged: if (containCheckBox.checked !== cfg_Contain) {
        containCheckBox.checked = cfg_Contain;
    }

    RowLayout {
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Plik wideo:")
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: videoFileField
            Layout.fillWidth: true
            text: root.cfg_VideoFile
            placeholderText: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Ścieżka lub adres URL pliku wideo")
            onTextEdited: root.cfg_VideoFile = text
        }

        QQC2.Button {
            icon.name: "document-open"
            text: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Wybierz...")
            onClicked: {
                if (root.cfg_VideoFile.startsWith("file://")) {
                    fileDialog.selectedFile = root.cfg_VideoFile;
                }
                fileDialog.open();
            }
        }
    }

    QQC2.ComboBox {
        id: hdrModeComboBox
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Tryb HDR:")
        textRole: "label"
        valueRole: "value"
        // Wartości zgodne z kontraktem HdrVideoSurface.hdrMode: 0=auto, 1=force, 2=off.
        model: [
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Automatyczny"), value: 0 },
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Wymuś HDR"), value: 1 },
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "SDR"), value: 2 }
        ]
        onActivated: root.cfg_HdrMode = currentValue
        Component.onCompleted: currentIndex = indexOfValue(root.cfg_HdrMode)
    }

    QQC2.SpinBox {
        id: peakSpinBox
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Jasność szczytowa (nity):")
        from: 100
        to: 10000
        stepSize: 50
        editable: true
        value: root.cfg_Peak
        onValueModified: root.cfg_Peak = value
    }

    Item {
        Kirigami.FormData.isSection: true
    }

    QQC2.CheckBox {
        id: sdr2hdrCheckBox
        text: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "SDR→HDR (inverse tone mapping)")
        checked: root.cfg_Sdr2Hdr
        onToggled: root.cfg_Sdr2Hdr = checked
    }

    QQC2.CheckBox {
        id: containCheckBox
        text: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Dopasuj z pasami (contain)")
        checked: root.cfg_Contain
        onToggled: root.cfg_Contain = checked
    }

    QtDialogs.FileDialog {
        id: fileDialog
        title: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Wybierz plik wideo")
        fileMode: QtDialogs.FileDialog.OpenFile
        options: QtDialogs.FileDialog.ReadOnly
        nameFilters: [
            i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Pliki wideo") + " (*.mp4 *.mkv *.webm *.mov)",
            i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Wszystkie pliki") + " (*)"
        ]
        onAccepted: root.cfg_VideoFile = selectedFile.toString()
    }
}
