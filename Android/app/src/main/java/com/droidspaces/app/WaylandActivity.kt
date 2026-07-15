package com.droidspaces.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.droidspaces.app.ui.screen.WaylandScreen

/*
 * Dedicated launcher Activity for Wayland.
 *
 * Uses its own taskAffinity so Android treats it as
 * a separate application in Launcher and Recents,
 * while still sharing the same process and data.
 */
class WaylandActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            ThemeWrapper {

                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    WaylandScreen(
                        onNavigateBack = {
                            finish()
                        }
                    )
                }

            }
        }
    }
}
