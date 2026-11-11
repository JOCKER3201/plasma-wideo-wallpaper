/*
    SPDX-FileCopyrightText: 2026 JOCKER3201

    SPDX-License-Identifier: MIT

    Configuration UI of the "HDR Video" wallpaper (io.github.jocker3201.hdrvideo).

    The cfg_ convention: on open, Plasma's configuration dialog
    (ConfigurationContainmentAppearance.qml) injects the values of the keys from
    contents/config/main.xml into cfg_<Key> properties as initial properties, and
    on OK/Apply it writes them back to wallpaperConfiguration. The property types
    MUST match the kcfg types.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import io.github.jocker3201.hdrvideo 1.0

Kirigami.FormLayout {
    id: root

    // HDR capability probe (protocol, PQ/BT.2020, screens' HDR mode) — when HDR
    // cannot work, the HDR options are greyed out and an InlineMessage says why.
    HdrCapabilities {
        id: hdrCaps
    }

    // Column alignment with the dialog's other FormLayouts; "parentLayout" is a
    // property of the ancestor with id "appearanceRoot" (dialog context), as in
    // org.kde.color.
    twinFormLayouts: parentLayout
    property alias formLayout: root

    // Injected by the dialog as initial properties (declaring them silences warnings).
    property var configDialog
    property var wallpaperConfiguration

    // Keys from main.xml, group General — types matching the kcfg.
    property string cfg_VideoFile   // String: path/URL of the video file
    property int cfg_HdrMode        // Int: 0=auto, 1=force HDR, 2=SDR (off)
    property int cfg_Peak           // Int: peak luminance in nits
    property bool cfg_Sdr2Hdr       // Bool: SDR→HDR inverse tone mapping
    property bool cfg_Contain       // Bool: letterboxed fit instead of cropping

    // <Key>Default pseudo-keys from KConfigPropertyMap (org.kde.image pattern);
    // the dialog's dirty-check skips properties ending in "Default".
    property string cfg_VideoFileDefault
    property int cfg_HdrModeDefault
    property int cfg_PeakDefault
    property bool cfg_Sdr2HdrDefault
    property bool cfg_ContainDefault

    // Re-synchronize the controls when cfg_* changes outside of them
    // (e.g. the dialog's "Restore defaults" button).
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
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Video file:")
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: videoFileField
            Layout.fillWidth: true
            text: root.cfg_VideoFile
            placeholderText: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Path or URL of the video file")
            onTextEdited: root.cfg_VideoFile = text
        }

        QQC2.Button {
            icon.name: "document-open"
            text: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Browse...")
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
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "HDR mode:")
        enabled: hdrCaps.usable
        textRole: "label"
        valueRole: "value"
        // Values follow the HdrVideoSurface.hdrMode contract: 0=auto, 1=force, 2=off.
        model: [
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Automatic"), value: 0 },
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Force HDR"), value: 1 },
            { label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "SDR"), value: 2 }
        ]
        onActivated: root.cfg_HdrMode = currentValue
        Component.onCompleted: currentIndex = indexOfValue(root.cfg_HdrMode)
    }

    QQC2.SpinBox {
        id: peakSpinBox
        Kirigami.FormData.label: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Peak brightness (nits):")
        enabled: hdrCaps.usable
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
        enabled: hdrCaps.usable
        checked: root.cfg_Sdr2Hdr
        onToggled: root.cfg_Sdr2Hdr = checked
    }

    QQC2.CheckBox {
        id: containCheckBox
        text: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Fit with letterboxing (contain)")
        checked: root.cfg_Contain
        onToggled: root.cfg_Contain = checked
    }

    Item {
        Kirigami.FormData.isSection: true
    }

    // Why HDR does not work (options above greyed out) / screen status.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !hdrCaps.usable
        type: Kirigami.MessageType.Warning
        text: hdrCaps.reason
    }
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: hdrCaps.usable && hdrCaps.details.length > 0
        type: Kirigami.MessageType.Information
        text: hdrCaps.details
    }

    QtDialogs.FileDialog {
        id: fileDialog
        title: i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Choose a video file")
        fileMode: QtDialogs.FileDialog.OpenFile
        options: QtDialogs.FileDialog.ReadOnly
        nameFilters: [
            i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "Video files") + " (*.mp4 *.mkv *.webm *.mov)",
            i18nd("plasma_wallpaper_io.github.jocker3201.hdrvideo", "All files") + " (*)"
        ]
        onAccepted: root.cfg_VideoFile = selectedFile.toString()
    }
}
