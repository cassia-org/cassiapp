package cassia.app.input

import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View

/**
 * Handles all motion and key events from the surface and translates them into the appropriate input events for the compositor.
 * @note This class is not thread-safe and must only be called from a single thread.
 * @todo The class simply logs the events for now. It needs to be hooked up to the compositor.
 */
class InputHandler {
    companion object {
        const val TAG = "cassia.kt.InputHandler"
    }

    private fun moveMouse(dx: Float, dy: Float) {
        if (dx == 0f && dy == 0f)
            return
        Log.d(TAG, "Mouse Move: $dx, $dy")
    }

    private fun scrollMouse(dh: Float, dv: Float) {
        if (dh == 0f && dv == 0f)
            return
        Log.d(TAG, "Mouse Scroll: $dh, $dv")
    }

    private var mouseButtonState: Int = 0

    private fun updateButtonsMouse(buttonState: Int) {
        if (this.mouseButtonState == buttonState)
            return

        // All buttons are a single bit in the button state, so we can just check each one.
        for (i in 0..7) {
            val mask = 1 shl i
            if (buttonState and mask != this.mouseButtonState and mask) {
                val linuxButton = InputEventMap.toLinuxButton(i)
                if (buttonState and mask != 0) {
                    Log.d(TAG, "Mouse Button Down: $i -> $linuxButton")
                } else {
                    Log.d(TAG, "Mouse Button Up: $i -> $linuxButton")
                }
            }
        }

        this.mouseButtonState = buttonState
    }

    private fun pointerDown(pointerId: Int, x: Double, y: Double) {
        Log.d(TAG, "Touch Down [$pointerId]: $x, $y")
    }

    private fun pointerMove(pointerId: Int, x: Double, y: Double) {
        Log.d(TAG, "Touch Move [$pointerId]: $x, $y")
    }

    private fun pointerUp(pointerId: Int) {
        Log.d(TAG, "Touch Up [$pointerId]")
    }

    fun onMotionEvent(view: View, event: MotionEvent): Boolean {
        when (event.source and InputDevice.SOURCE_CLASS_MASK) {
            InputDevice.SOURCE_CLASS_POINTER -> {
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        pointerDown(event.getPointerId(event.actionIndex), event.getX(event.actionIndex).toDouble(), event.getY(event.actionIndex).toDouble())
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        pointerUp(event.getPointerId(event.actionIndex))
                    }

                    MotionEvent.ACTION_MOVE, MotionEvent.ACTION_HOVER_MOVE -> {
                        for (i in 0 until event.pointerCount) {
                            pointerMove(event.getPointerId(i), event.getX(i).toDouble(), event.getY(i).toDouble())
                        }
                    }
                }

                return true
            }

            InputDevice.SOURCE_CLASS_TRACKBALL -> {
                // Actions can be interleaved such as changing button state during a move, so we need to check all of them.
                moveMouse(event.x, event.y)
                scrollMouse(event.getAxisValue(MotionEvent.AXIS_HSCROLL), event.getAxisValue(MotionEvent.AXIS_VSCROLL))
                updateButtonsMouse(event.buttonState)

                return true
            }
        }
        return false
    }

    private fun keyEvent(scanCode: Int, down: Boolean) {
        Log.d("cassia.app", "Key $scanCode = $down")
    }

    fun onKeyEvent(event: KeyEvent): Boolean {
        when (event.source and InputDevice.SOURCE_CLASS_MASK) {
            InputDevice.SOURCE_CLASS_BUTTON -> {
                val scanCode = InputEventMap.toScanCode(event.keyCode)
                if (scanCode == 0)
                    return false // We don't recognize this key, so let the app handle it.

                keyEvent(scanCode, event.action == KeyEvent.ACTION_DOWN)
                return true
            }

            InputDevice.SOURCE_CLASS_TRACKBALL -> {
                // We want to consume any back/forward key events coming from a mouse so they can go to the app instead of being used for navigation.
                // These events don't need to be manually sent to the app because they are already sent as part of the mouse button state with motion events.
                return true
            }
        }

        return false
    }
}
