pragma Singleton
import QtQuick

// Which result the full-window viewer shows (a job from studio.jobs), or null.
QtObject {
    property var job: null
    function open(j) { job = j }
    function close() { job = null }
}
