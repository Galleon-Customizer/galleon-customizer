// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
Item {
    id: root
    property var layout: []
    property var values: ({})
    property date now: new Date()
    property var locale: Qt.locale()
    property string timeFormat: locale.timeFormat(Locale.ShortFormat)
    property color textColor: "white"
    Repeater {
        model: root.layout
        delegate: Rectangle {
            id: card
            required property var modelData
            readonly property real textScale: (modelData.textSize || 32) / 32
            readonly property real textSize: (modelData.textSize || 32) * root.height / 384
            readonly property bool clock: modelData.id === "clock"
            readonly property bool quota: modelData.id === "claude" || modelData.id === "codex"
            readonly property bool custom: quota || modelData.id === "volume"
            readonly property var info: root.values[modelData.id]
            readonly property real unit: root.height / 384
            x: modelData.x * root.width / 720
            y: modelData.y * root.height / 384
            width: modelData.width * root.width / 720
            height: modelData.height * root.height / 384
            visible: modelData.enabled
            color: clock ? "transparent" : "#db202528"
            radius: 10 * root.height / 384
            clip: true
            Column {
                visible: !card.custom
                anchors.fill: parent
                anchors.margins: card.clock ? 0 : 4 * root.height / 384
                spacing: 2 * root.height / 384
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    height: parent.height * (card.clock ? 0.74 : 0.34)
                    color: root.textColor
                    font.pixelSize: card.clock ? Math.min(card.height * 0.68, card.width * 0.30) * card.textScale : card.textSize * 0.56
                    font.bold: card.clock
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 8
                    elide: Text.ElideRight
                    text: card.custom ? "" : card.clock ? root.now.toLocaleTimeString(root.locale, card.modelData.hour24 ? (root.timeFormat.indexOf("ss") >= 0 ? "HH:mm:ss" : "HH:mm") : root.timeFormat)
                        : ({weather: root.values.weatherCity || qsTr("Weather"), cpu: qsTr("CPU"), memory: qsTr("RAM"), temperature: qsTr("CPU temperature"), downloads: qsTr("Downloads"), fps: qsTr("Game FPS")})[card.modelData.id]
                }
                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    height: parent.height * (card.clock ? 0.24 : 0.62)
                    color: root.textColor
                    font.bold: !card.clock
                    opacity: card.clock ? 0.8 : 1
                    font.pixelSize: card.clock ? Math.min(card.height * 0.17, card.width * 0.065) * card.textScale : card.textSize
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 8
                    elide: Text.ElideRight
                    text: card.custom ? "" : card.clock ? root.now.toLocaleDateString(root.locale, root.locale.dateFormat(Locale.LongFormat))
                        : (root.values[card.modelData.id] || qsTr("Unavailable"))
                }
            }
            // System volume: the right dial, as on the Windows profile.
            Item {
                visible: card.modelData.id === "volume"
                anchors.fill: parent
                anchors.margins: 12 * card.unit
                readonly property int percent: (card.info && card.info.percent) || 0
                readonly property bool muted: !!(card.info && card.info.muted)
                Text {
                    id: volumeTitle
                    textFormat: Text.PlainText
                    color: root.textColor
                    opacity: 0.8
                    font.pixelSize: 22 * card.unit
                    text: qsTr("Volume")
                }
                Text {
                    // Below the title row: the top right corner is where the theme name goes.
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -4 * card.unit
                    textFormat: Text.PlainText
                    color: parent.muted ? "#EF4444" : root.textColor
                    font.bold: true
                    font.pixelSize: 44 * card.unit
                    text: !card.info ? qsTr("Unavailable") : parent.muted ? qsTr("Muted") : parent.percent + "%"
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 22 * card.unit
                    radius: height / 2
                    color: "#1F2937"
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        width: parent.width * Math.max(0, Math.min(100, parent.parent.percent)) / 100
                        color: parent.parent.muted ? "#6B7280" : "#3B82F6"
                    }
                }
            }
            // AI quota (Quota Control): two accounts, each with its limits left.
            Item {
                id: quotaPanel
                visible: card.quota
                anchors.fill: parent
                anchors.margins: 8 * card.unit
                readonly property var accounts: card.info && card.info.accounts ? card.info.accounts : []
                readonly property string logo: card.modelData.id === "claude" ? "logos/claude.png" : "logos/codex.png"
                readonly property color accent: card.modelData.id === "claude" ? "#D97757" : "#10A37F"
                Text {
                    visible: quotaPanel.accounts.length === 0
                    anchors.centerIn: parent
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    color: root.textColor
                    opacity: 0.8
                    font.pixelSize: 18 * card.unit
                    text: card.info && card.info.message ? card.info.message : qsTr("Waiting for Quota Control")
                }
                Column {
                    anchors.fill: parent
                    spacing: 4 * card.unit
                    Repeater {
                        model: quotaPanel.accounts
                        delegate: Column {
                            id: account
                            required property var modelData
                            required property int index
                            width: quotaPanel.width
                            height: (quotaPanel.height - 4 * card.unit) / 2
                            spacing: 3 * card.unit
                            Rectangle {
                                visible: account.index > 0
                                width: parent.width
                                height: 1
                                color: "#1F2937"
                            }
                            Row {
                                width: parent.width
                                height: 26 * card.unit
                                spacing: 6 * card.unit
                                Image {
                                    source: quotaPanel.logo
                                    width: 22 * card.unit
                                    height: width
                                    anchors.verticalCenter: parent.verticalCenter
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }
                                Text {
                                    width: parent.width * 0.55
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: root.textColor
                                    font.bold: true
                                    font.pixelSize: 19 * card.unit
                                    text: account.modelData.account
                                }
                                Text {
                                    width: parent.width * 0.45 - 34 * card.unit
                                    anchors.verticalCenter: parent.verticalCenter
                                    horizontalAlignment: Text.AlignRight
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: quotaPanel.accent
                                    font.pixelSize: 15 * card.unit
                                    text: account.modelData.plan || ""
                                }
                            }
                            Text {
                                visible: !account.modelData.found
                                textFormat: Text.PlainText
                                color: root.textColor
                                opacity: 0.7
                                font.pixelSize: 15 * card.unit
                                text: qsTr("Not found in Quota Control")
                            }
                            Repeater {
                                model: account.modelData.rows || []
                                delegate: Row {
                                    id: limit
                                    required property var modelData
                                    readonly property real remaining: modelData.remaining
                                    readonly property color level: account.modelData.stale ? "#6B7280"
                                        : remaining >= 50 ? "#22C55E" : remaining >= 20 ? "#F59E0B" : "#EF4444"
                                    width: account.width
                                    height: 22 * card.unit
                                    spacing: 6 * card.unit
                                    Text {
                                        width: 44 * card.unit
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        color: root.textColor
                                        opacity: 0.8
                                        font.pixelSize: 15 * card.unit
                                        text: limit.modelData.label
                                    }
                                    Rectangle {
                                        width: parent.width - 108 * card.unit
                                        height: 12 * card.unit
                                        radius: height / 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: "#1F2937"
                                        Rectangle {
                                            height: parent.height
                                            radius: parent.radius
                                            width: parent.width * Math.max(0, Math.min(100, limit.remaining)) / 100
                                            color: limit.level
                                        }
                                    }
                                    Text {
                                        width: 46 * card.unit
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: Text.AlignRight
                                        textFormat: Text.PlainText
                                        color: limit.level
                                        font.bold: true
                                        font.pixelSize: 16 * card.unit
                                        text: Math.round(limit.remaining) + "%"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
