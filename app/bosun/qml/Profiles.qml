// SPDX-License-Identifier: GPL-3.0-or-later

// Profiles: copies of every page, key and tab, saved by name and used again later.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: profiles
    required property var backend
    signal answered(string error, string done)
    property string name: ""
    spacing: 10

    Text {
        color: "white"
        font.pixelSize: 18
        font.bold: true
        text: qsTr("Profiles")
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("A profile is a copy of every page, key and tab. Using one replaces them (the previous file is kept as .bak).")
    }
    Row {
        spacing: 10
        Field {
            width: 220
            maximumLength: 40
            placeholder: qsTr("Name of the profile")
            value: profiles.name
            input.onTextEdited: profiles.name = input.text
        }
        Pill {
            text: qsTr("Save the keys as a profile")
            enabledLook: profiles.name.trim() !== ""
            onClicked: profiles.answered(profiles.backend.saveProfile(profiles.name.trim()), qsTr("Profile saved."))
        }
    }
    Repeater {
        model: profiles.backend.profiles
        delegate: Row {
            id: row
            required property string modelData
            spacing: 10
            Text {
                width: 220
                height: 30
                verticalAlignment: Text.AlignVCenter
                color: "white"
                font.pixelSize: 15
                elide: Text.ElideRight
                text: row.modelData
            }
            Pill {
                selected: true
                text: qsTr("Use")
                onClicked: profiles.answered(profiles.backend.loadProfile(row.modelData), qsTr("Profile in use."))
            }
            Pill {
                danger: true
                text: qsTr("Remove")
                onClicked: profiles.answered(profiles.backend.removeProfile(row.modelData), qsTr("Profile removed."))
            }
        }
    }
}
