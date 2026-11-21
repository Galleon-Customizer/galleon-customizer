// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// What one key does and shows. The person picks the kind of action, fills what it needs, and
// presses Apply: the pages file is written and galleon-customizerd (when it runs) shows the change at once.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: editor

    property var filePicker: null
    required property var backend
    required property string page
    required property var key // the entry of backend.keys(page) for the selected key
    // The answer of an edit: the reason when it was refused, else what was done.
    signal answered(string error, string done)

    // What is being edited (the fields of a KeySpec).
    property var spec: ({})
    property string appFilter: ""
    readonly property var kinds: [
        {id: "app", label: qsTr("Application")},
        {id: "media", label: qsTr("Music player")},
        {id: "page", label: qsTr("Open a page")},
        {id: "back", label: qsTr("Back")},
        {id: "key", label: qsTr("Keypad key")},
        {id: "combo", label: qsTr("Shortcut")},
        {id: "text", label: qsTr("Type text")},
        {id: "url", label: qsTr("Web address")},
        {id: "file", label: qsTr("File or folder")},
        {id: "command", label: qsTr("Command")},
        {id: "builtin", label: qsTr("Ready-made")}
    ]
    readonly property var builtinSpec: backend.builtinActions.find(b => b.id === spec.builtin) || null
    readonly property bool complete: {
        switch (spec.type) {
        case "app": return spec.desktop !== ""
        case "media": return spec.command !== ""
        case "page": return spec.page !== ""
        case "back": return true
        case "key": return spec.keyName !== ""
        case "steam": return true
        case "keep": return true
        case "combo": return (spec.keys || "").trim() !== ""
        case "text": return (spec.text || "") !== ""
        case "url": return (spec.url || "").trim() !== ""
        case "file": return (spec.url || "").trim() !== ""
        case "command": return (spec.shell || "").trim() !== ""
        case "builtin": return (spec.builtin || "") !== ""
        default: return false
        }
    }
    readonly property var shownApps: {
        const needle = appFilter.trim().toLowerCase()
        return backend.installedApps().filter(a => needle === "" || a.name.toLowerCase().indexOf(needle) >= 0)
    }

    spacing: 12

    function change(name, value) {
        const copy = Object.assign({}, spec)
        copy[name] = value
        spec = copy
    }

    function load() {
        const k = key
        const builtin = /^(system|sound|clipboard|capture)\./.test(k.type || "")
        const kept = k.type === "multi" || k.type === "toggle"
        spec = {
            type: k.empty || k.type === "none" ? "" : (builtin ? "builtin" : (kept ? "keep" : k.type)),
            keptKind: kept ? k.type : "",
            builtin: builtin ? k.type : "",
            options: k.options || ({}),
            keys: k.keys || "",
            text: k.text || "",
            enter: k.enter || false,
            url: k.url || "",
            shell: k.shell || "",
            terminal: k.terminal || false,
            desktop: k.desktop || "",
            command: k.command || "",
            step: k.step || 5,
            seconds: k.seconds || 10,
            page: k.pageTarget || "",
            keyName: k.keyName || "",
            appid: k.appid || 0,
            label: k.rawLabel || "",
            icon: k.rawIcon || ""
        }
        appFilter = ""
        Qt.callLater(editor.reveal)
    }

    // The list of applications opens on the one the key has.
    function reveal() {
        const place = shownApps.findIndex(a => a.id === spec.desktop)
        if (place >= 0)
            apps.positionViewAtIndex(place, ListView.Center)
    }

    function kindPicked(id) {
        const copy = Object.assign({}, spec)
        copy.type = id
        if (id === "media" && copy.command === "")
            copy.command = "play-pause"
        spec = copy
    }

    function builtinPicked(id) {
        const copy = Object.assign({}, spec)
        copy.builtin = id
        copy.options = ({})
        spec = copy
    }

    function setOption(name, value) {
        const options = Object.assign({}, spec.options)
        if (value === undefined || value === "")
            delete options[name]
        else
            options[name] = value
        change("options", options)
    }

    // Backend refreshes also rebuild identical key objects. Keep the draft until
    // the selected page or the saved key actually changes.
    readonly property string savedKey: JSON.stringify([page, key])
    onSavedKeyChanged: load()
    Component.onCompleted: load()

    Text {
        color: "white"
        font.pixelSize: 18
        font.bold: true
        text: qsTr("Key %1").arg(editor.key.index + 1)
    }

    // ---- what the key does
    Text {
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("It does")
    }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.kinds
            delegate: Pill {
                required property var modelData
                text: modelData.label
                selected: editor.spec.type === modelData.id
                onClicked: editor.kindPicked(modelData.id)
            }
        }
        Pill {
            visible: editor.spec.type === "steam"
            text: qsTr("Steam game")
            selected: true
        }
        Pill {
            visible: editor.spec.type === "keep"
            text: editor.spec.keptKind === "toggle" ? qsTr("On / off") : qsTr("Several actions")
            selected: true
            onClicked: editor.kindPicked("keep")
        }
    }

    // A shortcut, a text to type, an address, a file, a command line.
    Column {
        visible: ["combo", "text", "url", "file", "command"].indexOf(editor.spec.type) >= 0
        width: parent.width
        spacing: 6
        Field {
            width: Math.min(parent.width, 420)
            maximumLength: editor.spec.type === "command" ? 1000 : (editor.spec.type === "text" ? 200 : 2000)
            placeholder: editor.spec.type === "combo" ? qsTr("Keys such as ctrl+shift+s")
                       : editor.spec.type === "text" ? qsTr("Text")
                       : editor.spec.type === "url" ? "https://…"
                       : editor.spec.type === "file" ? qsTr("Absolute path, or ~/…")
                       : qsTr("Command line")
            value: editor.spec.type === "combo" ? editor.spec.keys
                 : editor.spec.type === "text" ? editor.spec.text
                 : editor.spec.type === "command" ? editor.spec.shell : editor.spec.url
            input.onTextEdited: editor.change(editor.spec.type === "combo" ? "keys"
                                            : editor.spec.type === "text" ? "text"
                                            : editor.spec.type === "command" ? "shell" : "url", input.text)
        }
        Pill {
            visible: editor.spec.type === "text"
            text: qsTr("Then press Enter")
            selected: editor.spec.enter
            onClicked: editor.change("enter", !editor.spec.enter)
        }
        Pill {
            visible: editor.spec.type === "command"
            text: qsTr("In a terminal")
            selected: editor.spec.terminal
            onClicked: editor.change("terminal", !editor.spec.terminal)
        }
    }

    // A ready-made action, then its options.
    Column {
        visible: editor.spec.type === "builtin"
        width: parent.width
        spacing: 6
        Repeater {
            model: ["system", "sound", "clipboard", "capture"]
            delegate: Flow {
                id: group
                required property string modelData
                readonly property var actions: editor.backend.builtinActions.filter(b => b.id.indexOf(modelData + ".") === 0)
                width: editor.width
                spacing: 6
                Text {
                    width: 110
                    height: 30
                    verticalAlignment: Text.AlignVCenter
                    color: "#9aa0a6"
                    font.pixelSize: 13
                    text: group.actions.length > 0 ? group.actions[0].group : ""
                }
                Repeater {
                    model: group.actions
                    delegate: Pill {
                        required property var modelData
                        text: modelData.label
                        selected: editor.spec.builtin === modelData.id
                        onClicked: editor.builtinPicked(modelData.id)
                    }
                }
            }
        }
        Repeater {
            model: editor.builtinSpec ? editor.builtinSpec.options : []
            delegate: Flow {
                id: option
                required property var modelData
                width: editor.width
                spacing: 6
                Text {
                    width: 110
                    height: 30
                    verticalAlignment: Text.AlignVCenter
                    color: "#9aa0a6"
                    font.pixelSize: 13
                    text: option.modelData.key
                }
                Repeater {
                    model: option.modelData.kind === "choice" ? option.modelData.choices : []
                    delegate: Pill {
                        required property string modelData
                        text: modelData
                        selected: (editor.spec.options[option.modelData.key] || option.modelData.choices[0]) === modelData
                        onClicked: editor.setOption(option.modelData.key, modelData)
                    }
                }
                Pill {
                    visible: option.modelData.kind === "bool"
                    text: editor.spec.options[option.modelData.key] === false ? qsTr("Off") : qsTr("On")
                    selected: editor.spec.options[option.modelData.key] !== false
                    onClicked: editor.setOption(option.modelData.key, editor.spec.options[option.modelData.key] === false)
                }
                Field {
                    visible: option.modelData.kind === "int" || option.modelData.kind === "string"
                    width: option.modelData.kind === "int" ? 90 : 300
                    maximumLength: option.modelData.kind === "int" ? 5 : option.modelData.max
                    placeholder: option.modelData.kind === "int" ? option.modelData.min + "…" + option.modelData.max : ""
                    value: editor.spec.options[option.modelData.key] !== undefined ? String(editor.spec.options[option.modelData.key]) : ""
                    input.onTextEdited: editor.setOption(option.modelData.key,
                        option.modelData.kind === "int" ? (input.text === "" || isNaN(parseInt(input.text)) ? undefined : parseInt(input.text)) : input.text)
                }
            }
        }
    }

    // An application: search the installed ones.
    Column {
        visible: editor.spec.type === "app"
        width: parent.width
        spacing: 6
        Pill {
            text: qsTr("Browse…")
            enabledLook: editor.filePicker !== null
            onClicked: {
                const path = editor.filePicker.application()
                if (path !== "") editor.change("desktop", path)
            }
        }
        Field {
            width: parent.width
            placeholder: qsTr("Search an application…")
            onEdited: text => editor.appFilter = text
            input.onTextChanged: editor.appFilter = input.text
        }
        ListView {
            id: apps
            width: parent.width
            height: 210
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: editor.shownApps
            delegate: Rectangle {
                id: appRow
                required property var modelData
                width: apps.width
                height: 38
                radius: 6
                color: editor.spec.desktop === modelData.id ? "#2e7d32" : (area.containsMouse ? "#2a2d32" : "transparent")
                Image {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    source: "image://themeicon/" + (appRow.modelData.icon !== "" ? appRow.modelData.icon : "application-x-executable")
                    sourceSize.width: 52
                    sourceSize.height: 52
                    asynchronous: true
                }
                Text {
                    x: 44
                    width: parent.width - 52
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    color: "white"
                    font.pixelSize: 14
                    text: appRow.modelData.name
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: editor.change("desktop", appRow.modelData.id)
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: appRow.modelData.name
            }
        }
        Text {
            visible: editor.spec.desktop !== ""
            width: parent.width
            elide: Text.ElideMiddle
            color: "#7d8591"
            font.pixelSize: 12
            text: editor.spec.desktop
        }
    }

    // The music player.
    Column {
        visible: editor.spec.type === "media"
        width: parent.width
        spacing: 8
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: editor.backend.mediaCommands
                delegate: Pill {
                    required property var modelData
                    text: modelData.label
                    selected: editor.spec.command === modelData.id
                    onClicked: editor.change("command", modelData.id)
                }
            }
        }
        Row {
            visible: editor.spec.command === "volume"
            spacing: 12
            Text {
                height: 32
                verticalAlignment: Text.AlignVCenter
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Each press changes it by")
            }
            Stepper {
                value: editor.spec.step
                from: -100
                to: 100
                step: 5
                unit: "%"
                onPicked: v => editor.change("step", v === 0 ? (editor.spec.step > 0 ? -5 : 5) : v)
            }
        }
        Row {
            visible: editor.spec.command === "seek"
            spacing: 12
            Text {
                height: 32
                verticalAlignment: Text.AlignVCenter
                color: "#c9ced6"
                font.pixelSize: 14
                text: qsTr("Each press moves by")
            }
            Stepper {
                value: editor.spec.seconds
                from: -3600
                to: 3600
                step: 5
                unit: qsTr("s")
                onPicked: v => editor.change("seconds", v === 0 ? (editor.spec.seconds > 0 ? -5 : 5) : v)
            }
        }
    }

    // Another page.
    Flow {
        visible: editor.spec.type === "page"
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.backend.pageNames.filter(name => name !== editor.page)
            delegate: Pill {
                required property string modelData
                text: modelData
                selected: editor.spec.page === modelData
                onClicked: editor.change("page", modelData)
            }
        }
    }

    // A key of the numeric keypad.
    Flow {
        visible: editor.spec.type === "key"
        width: parent.width
        spacing: 6
        Repeater {
            model: editor.backend.keypadKeys
            delegate: Pill {
                required property string modelData
                text: modelData
                selected: editor.spec.keyName === modelData
                onClicked: editor.change("keyName", modelData)
            }
        }
    }

    Text {
        visible: editor.spec.type === "back"
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("Goes back to the page that was shown before.")
    }
    Text {
        visible: editor.spec.type === "steam"
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("Starts the Steam game number %1. It is kept as it is; give the key another action to replace it.").arg(editor.spec.appid)
    }
    Text {
        visible: editor.spec.type === "keep"
        width: parent.width
        wrapMode: Text.Wrap
        color: "#9aa0a6"
        font.pixelSize: 13
        text: qsTr("This key does several actions, or switches on and off. It is kept as it is; its text and icon can still be changed.")
    }

    // ---- what it shows
    Row {
        visible: editor.spec.type !== ""
        spacing: 14
        Column {
            spacing: 4
            Text {
                color: "#9aa0a6"
                font.pixelSize: 13
                text: qsTr("Text (empty: the default)")
            }
            Field {
                width: 190
                maximumLength: 24
                placeholder: qsTr("Text")
                value: editor.spec.label
                input.onTextEdited: editor.change("label", input.text)
            }
        }
        Column {
            spacing: 4
            Text {
                color: "#9aa0a6"
                font.pixelSize: 13
                text: qsTr("Icon name (empty: the default)")
            }
            Row {
                spacing: 8
                Field {
                    width: 190
                    maximumLength: 64
                    placeholder: qsTr("icon-name")
                    value: editor.spec.icon
                    input.onTextEdited: editor.change("icon", input.text)
                }
                Image {
                    width: 34
                    height: 34
                    visible: editor.spec.icon !== ""
                    source: editor.spec.icon !== "" ? "image://themeicon/" + editor.spec.icon : ""
                    sourceSize.width: 68
                    sourceSize.height: 68
                }
            }
        }
    }

    // ---- apply
    Row {
        spacing: 10
        Pill {
            selected: true
            text: qsTr("Apply")
            enabledLook: editor.complete
            onClicked: editor.answered(editor.backend.setKey(editor.page, editor.key.index, editor.spec), qsTr("Key saved."))
        }
        Pill {
            text: qsTr("Empty the key")
            danger: true
            enabledLook: !editor.key.empty
            onClicked: editor.answered(editor.backend.clearKey(editor.page, editor.key.index), qsTr("Key emptied."))
        }
        Pill {
            text: qsTr("Undo")
            onClicked: editor.load()
        }
    }
}
