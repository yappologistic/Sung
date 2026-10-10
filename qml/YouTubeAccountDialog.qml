import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "youtubeAccountDialog"
    title: "YouTube Music"
    modal: true
    anchors.centerIn: parent
    width: Math.min(480, parent.width - 48)
    implicitHeight: header.implicitHeight + contentItem.implicitHeight + (footer ? footer.implicitHeight : 0) + topPadding + bottomPadding
    standardButtons: Dialog.NoButton

    property bool confirmSignOut: false

    onAboutToShow: {
        confirmSignOut = false
        credsInput.text = ""
    }

    contentItem: ColumnLayout {
        id: contentLayout
        spacing: 16
        width: dialog.availableWidth

        // --- Logged in View ---
        ColumnLayout {
            id: loggedInView
            visible: app.ytLoggedIn && !dialog.confirmSignOut
            Layout.fillWidth: true
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Artwork {
                    id: userAvatar
                    width: 56
                    height: 56
                    radius: 28
                    url: app.ytAccountPhoto
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    SungText {
                        text: app.ytAccountName || "YouTube Music User"
                        font.pixelSize: Theme.titleMedium
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    SungText {
                        text: app.ytAccountHandle || "Connected"
                        font.pixelSize: Theme.bodySmall
                        color: Theme.muted
                        visible: !!text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    SungText {
                        text: app.ytLastSyncTime ? ("Last synced: " + app.ytLastSyncTime) : "Never synced"
                        font.pixelSize: Theme.bodySmall
                        color: Theme.muted
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            MDivider { Layout.fillWidth: true }

            MSwitch {
                id: syncStartupSwitch
                objectName: "ytSyncStartupSwitch"
                Layout.fillWidth: true
                text: "Sync library on startup"
                checked: app.ytSyncOnStartup
                onToggled: app.ytSyncOnStartup = checked
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                SungText {
                    text: app.ytSyncStatus || (app.ytSyncing ? "Syncing..." : "Ready to sync")
                    font.pixelSize: Theme.bodySmall
                    color: app.ytSyncStatus.indexOf("failed") !== -1 ? Theme.error : Theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                MButton {
                    id: syncButton
                    objectName: "ytSyncButton"
                    text: app.ytSyncing ? "Syncing…" : "Sync library"
                    enabled: !app.ytSyncing
                    tonal: true
                    onClicked: app.syncYouTubeLibrary()
                }
            }

            MDivider { Layout.fillWidth: true }

            MButton {
                id: signOutTrigger
                objectName: "ytSignOutTrigger"
                text: "Sign out…"
                Layout.alignment: Qt.AlignLeft
                onClicked: dialog.confirmSignOut = true
            }
        }

        // --- Sign Out Confirmation View ---
        ColumnLayout {
            id: signOutConfirmView
            visible: app.ytLoggedIn && dialog.confirmSignOut
            Layout.fillWidth: true
            spacing: 16

            SungText {
                text: "Sign out of YouTube Music"
                font.pixelSize: Theme.titleMedium
                font.weight: Font.DemiBold
            }

            SungText {
                text: "Choose what happens to music and playlists synced from YouTube Music:"
                font.pixelSize: Theme.bodyMedium
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                MButton {
                    objectName: "ytSignOutKeepButton"
                    text: "Keep synced music in library"
                    tonal: true
                    Layout.fillWidth: true
                    onClicked: {
                        dialog.confirmSignOut = false
                        app.logoutYouTube(false)
                        dialog.close()
                    }
                }

                MButton {
                    objectName: "ytSignOutClearButton"
                    text: "Clear synced music from library"
                    Layout.fillWidth: true
                    onClicked: {
                        dialog.confirmSignOut = false
                        app.logoutYouTube(true)
                        dialog.close()
                    }
                }

                MButton {
                    text: "Cancel"
                    Layout.fillWidth: true
                    onClicked: dialog.confirmSignOut = false
                }
            }
        }

        // --- Logged out / Sign in View ---
        ColumnLayout {
            id: loggedOutView
            visible: !app.ytLoggedIn
            Layout.fillWidth: true
            spacing: 14

            SungText {
                text: "Sign in with YouTube Music"
                font.pixelSize: Theme.titleMedium
                font.weight: Font.DemiBold
            }

            SungText {
                text: "Synchronize your liked songs and playlists. Your session remains strictly local on this device."
                font.pixelSize: Theme.bodyMedium
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            MButton {
                id: webLoginButton
                objectName: "ytWebLoginButton"
                text: app.ytSyncing ? "Signing in…" : "Sign in with Google"
                filled: true
                enabled: !app.ytSyncing
                Layout.fillWidth: true
                onClicked: app.loginYouTubeWeb()
            }

            SungText {
                text: app.ytSyncStatus
                visible: !!text
                color: app.ytSyncStatus.indexOf("failed") !== -1 || app.ytSyncStatus.indexOf("cancelled") !== -1 ? Theme.error : Theme.primary
                font.pixelSize: Theme.bodySmall
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            MDivider { Layout.fillWidth: true }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                SungText {
                    text: "Or enter session token manually:"
                    font.pixelSize: Theme.bodySmall
                    color: Theme.muted
                }

                MTextField {
                    id: credsInput
                    objectName: "ytCredentialsField"
                    Layout.fillWidth: true
                    label: "Session token or cookie"
                    supporting: "Paste session token, cookie text, or devtools headers"
                    enabled: !app.ytSyncing
                    onAccepted: {
                        if (credsInput.text.trim()) {
                            app.loginYouTube(credsInput.text.trim())
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    MButton {
                        text: "Paste"
                        enabled: !app.ytSyncing
                        onClicked: {
                            credsInput.selectAll()
                            credsInput.paste()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    MButton {
                        id: loginButton
                        objectName: "ytLoginButton"
                        text: "Sign in with token"
                        enabled: credsInput.text.trim().length > 0 && !app.ytSyncing
                        onClicked: {
                            app.loginYouTube(credsInput.text.trim())
                        }
                    }
                }
            }

            MDivider { Layout.fillWidth: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                SungText {
                    text: "Or use a Netscape cookie file:"
                    color: Theme.muted
                    font.pixelSize: Theme.bodySmall
                    Layout.fillWidth: true
                }

                MButton {
                    text: "Import cookies.txt"
                    enabled: !app.ytSyncing
                    onClicked: {
                        dialog.close()
                        window.openFileDialog("cookies")
                    }
                }
            }
        }
    }

    footer: Item {
        implicitHeight: 64
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            spacing: 8

            MButton {
                text: "Close"
                onClicked: dialog.close()
            }
        }
    }
}
