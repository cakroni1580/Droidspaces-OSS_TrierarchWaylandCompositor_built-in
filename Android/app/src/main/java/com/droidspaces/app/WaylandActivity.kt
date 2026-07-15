package com.droidspaces.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import com.droidspaces.app.ui.screen.WaylandScreen
import com.droidspaces.app.ui.theme.AppTheme

/*
 * PATCH:
 * Dedicated launcher Activity for Wayland.
 * Runs in its own Android task while sharing
 * the same application process.
 */
class WaylandActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            AppTheme {
                WaylandScreen(
                    onNavigateBack = { finish() }
                )
            }
        }
    }
}
