package com.droidspaces.app.ui.screen

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.util.HostCapabilities

private enum class InstallPhase { Installing, Success, Warning, Failed }

/* Material Expressive motion tokens. Spatial springs move size and position and
 * may settle with a hint of overshoot; effects springs drive fades and colour and
 * never overshoot. The app's material3 predates MotionScheme, so they live here. */
private const val SPATIAL_DAMPING = 0.8f
private const val SPATIAL_STIFFNESS = 380f
private const val EFFECTS_DAMPING = 1f
private const val EFFECTS_STIFFNESS = 1600f
private const val EFFECTS_SLOW_STIFFNESS = 800f

@Composable
fun InstallationScreen(
    appStateViewModel: AppStateViewModel,
    onInstallationComplete: () -> Unit
) {
    val context = LocalContext.current

    // Install orchestration and state live in AppStateViewModel, so they
    // survive rotation; these are Compose state reads and recompose.
    val isSuccess = appStateViewModel.isInstallSuccess
    val errorMessage = appStateViewModel.installErrorMessage
    val rebootRecommended = appStateViewModel.installRebootRecommended
    val kernelUnsupported = HostCapabilities.state.collectAsState().value?.requirementsMet == false

    // The ViewModel outlives this screen and performInstallation() resets the
    // previous outcome only once the effect below runs, which is after the
    // first frame. Until then, show the loader rather than the stale result.
    var started by rememberSaveable { mutableStateOf(false) }
    val phase = when {
        !started -> InstallPhase.Installing
        isSuccess && kernelUnsupported -> InstallPhase.Warning
        isSuccess -> InstallPhase.Success
        errorMessage != null -> InstallPhase.Failed
        else -> InstallPhase.Installing
    }
    val done = phase != InstallPhase.Installing

    // Completely block the back gesture in every state. This screen must be
    // left only via the Continue button, whose handler decides the next
    // destination and triggers the post-install refresh. A raw back-stack pop
    // would skip that and strand the user on a stale screen (e.g. the
    // "update available" card still showing after the update finished).
    BackHandler(enabled = true) {
        // Intentionally no-op while installing, on success and on error.
    }

    // Run the install orchestration (idempotent inside the ViewModel).
    LaunchedEffect(Unit) {
        started = true
        appStateViewModel.performInstallation()
    }

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            // Show the Continue button once the work is finished, whether it
            // succeeded or failed - it is the only accepted way off this screen.
            if (done) {
                PrimaryActionBottomBar(
                    label = context.getString(R.string.continue_button),
                    icon = if (isSuccess) Icons.Default.Check else Icons.AutoMirrored.Filled.ArrowForward,
                    onClick = onInstallationComplete
                )
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            // Hero slot: the morphing loader while work runs, then the result
            // icon settling into the same box so nothing on the screen jumps.
            AnimatedContent(
                targetState = phase,
                transitionSpec = {
                    (scaleIn(spring(SPATIAL_DAMPING, SPATIAL_STIFFNESS), initialScale = 0.9f) +
                        fadeIn(spring(EFFECTS_DAMPING, EFFECTS_STIFFNESS)))
                        .togetherWith(fadeOut(spring(EFFECTS_DAMPING, EFFECTS_STIFFNESS)))
                },
                label = "install_hero"
            ) { target ->
                Box(modifier = Modifier.size(LoadingSize.Hero.size), contentAlignment = Alignment.Center) {
                    when (target) {
                        InstallPhase.Installing -> LoadingIndicator(size = LoadingSize.Hero)
                        InstallPhase.Success -> Icon(
                            imageVector = Icons.Default.CheckCircle,
                            contentDescription = null,
                            modifier = Modifier.fillMaxSize(),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        // Installed, but a kernel that failed a MUST HAVE probe gets the warning, not the tick.
                        InstallPhase.Warning -> Icon(
                            imageVector = Icons.Default.Warning,
                            contentDescription = null,
                            modifier = Modifier.fillMaxSize(),
                            tint = MaterialTheme.colorScheme.error
                        )
                        InstallPhase.Failed -> Icon(
                            imageVector = Icons.Default.Error,
                            contentDescription = null,
                            modifier = Modifier.fillMaxSize(),
                            tint = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(32.dp))

            Text(
                text = context.getString(
                    when (phase) {
                        InstallPhase.Installing -> R.string.installing_droidspaces
                        InstallPhase.Failed -> R.string.installation_failed
                        else -> R.string.installation_complete
                    }
                ),
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center
            )

            // One outcome line once the work is done. Nothing is shown while
            // installing: the loader is the whole message.
            AnimatedVisibility(
                visible = done,
                enter = fadeIn(spring(EFFECTS_DAMPING, EFFECTS_SLOW_STIFFNESS)),
                exit = fadeOut(spring(EFFECTS_DAMPING, EFFECTS_STIFFNESS))
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Spacer(modifier = Modifier.height(12.dp))
                    Text(
                        text = when (phase) {
                            InstallPhase.Warning -> context.getString(R.string.backend_installed_unsupported_kernel)
                            InstallPhase.Failed -> errorMessage.orEmpty()
                            else -> context.getString(R.string.backend_installed_success)
                        },
                        style = MaterialTheme.typography.bodyLarge,
                        color = if (phase == InstallPhase.Success) MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            else MaterialTheme.colorScheme.error,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.fillMaxWidth()
                    )

                    // The flag is set before the steps start, so gate on the
                    // outcome or the note would sit under the loader the whole time.
                    if (phase == InstallPhase.Success && rebootRecommended) {
                        Spacer(modifier = Modifier.height(24.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Info,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.reboot_recommended),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.primary,
                                fontWeight = FontWeight.Medium,
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
            }
        }
    }
}
