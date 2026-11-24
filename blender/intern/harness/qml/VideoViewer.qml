import QtQuick
import QtMultimedia

// Plays a video result: click to play or pause, scrub along the bar, loops.
Item {
    id: root
    property url source
    MediaPlayer {
        id: player
        source: root.source
        videoOutput: out
        loops: MediaPlayer.Infinite
        Component.onCompleted: play()
    }
    VideoOutput { id: out; anchors.fill: parent; anchors.margins: 24; anchors.bottomMargin: 72 }
    TapHandler { onTapped: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
    Item {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 24
        x: 24; width: parent.width - 48; height: 32
        IconButton {
            id: pp
            anchors.verticalCenter: parent.verticalCenter
            icon: Theme.icon(player.playbackState === MediaPlayer.PlayingState ? "stop" : "play_arrow")
            onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
        }
        Rectangle {
            id: track
            anchors.left: pp.right; anchors.leftMargin: 12
            anchors.right: time.left; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: 4; radius: 2; color: Theme.border
            Rectangle { width: player.duration > 0 ? parent.width * player.position / player.duration : 0; height: parent.height; radius: 2; color: Theme.create }
            MouseArea { anchors.fill: parent; anchors.margins: -8; onPressed: (m) => player.position = player.duration * m.x / width; onPositionChanged: (m) => player.position = player.duration * Math.max(0, Math.min(1, m.x / width)) }
        }
        Text {
            id: time
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: (player.position / 1000).toFixed(1) + " / " + (player.duration / 1000).toFixed(1) + " s"
            color: Theme.textSecondary; font.family: Theme.font; font.pixelSize: Theme.sizeSmall; font.weight: Theme.weightUi; renderType: Text.NativeRendering
        }
    }
}
