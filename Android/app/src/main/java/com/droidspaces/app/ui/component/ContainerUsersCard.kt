package com.droidspaces.app.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.droidspaces.app.ui.component.DsDropdown
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.util.rememberClearFocus
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showSuccess
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerUsersCard(
    containerName: String,
    users: List<String>?,
    onRefresh: suspend () -> Unit,
    snackbarHostState: SnackbarHostState? = null,
    modifier: Modifier = Modifier
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val clearFocus = rememberClearFocus()

    // Null means the screen is still probing the container
    val isLoading = users == null

    // Available users list: always include "root", plus any local users
    val availableUsers = remember(users) {
        val userList = users.orEmpty().toMutableList()
        if (!userList.contains("root")) {
            userList.add(0, "root") // Add root at the beginning if not present
        }
        userList
    }

    // Same preselection as the terminal's user picker
    var selectedUser by remember {
        mutableStateOf(PreferencesManager.getInstance(context).getLastTerminalUser(containerName) ?: "root")
    }

    // Skipped while loading, or the remembered user is reset before the list arrives
    LaunchedEffect(users) {
        if (!isLoading && !availableUsers.contains(selectedUser)) {
            selectedUser = "root"
        }
    }


    Surface(
        modifier = modifier
            .fillMaxWidth()
            .animateContentSize(), // Smooth height changes when dropdown opens
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Title area - clickable to unfocus
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.clickable(
                        indication = null,
                        interactionSource = remember { MutableInteractionSource() }
                    ) {
                        clearFocus()
                    }
                ) {
                    Icon(
                        Icons.Default.Person,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = context.getString(R.string.available_users),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }

                DsRefreshButton(
                    contentDescription = context.getString(R.string.refresh_users),
                    onRefresh = {
                        clearFocus()
                        onRefresh()
                    },
                    modifier = Modifier.size(40.dp),
                    iconSize = 20.dp,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            // Show users list - use stable layout to prevent glitches
            if (isLoading) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 40.dp),
                    contentAlignment = Alignment.Center
                ) {
                    LoadingIndicator(size = LoadingSize.Medium)
                }
            } else if (users.isNullOrEmpty()) {
                Text(
                    text = context.getString(R.string.no_local_users_found),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.heightIn(min = 24.dp)
                )
            } else {
                Column(
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier
                        .heightIn(max = 150.dp)
                        .clickable(
                            indication = null,
                            interactionSource = remember { MutableInteractionSource() }
                        ) {
                            clearFocus()
                        }
                ) {
                    users.orEmpty().forEach { user ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = user,
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }
            }

            DsDropdown(
                label = context.getString(R.string.select_user),
                selected = selectedUser,
                options = availableUsers,
                displayName = { it },
                onSelect = { selectedUser = it }
            )

            // Copy login button - right aligned, same dimensions and style as Manage button
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End
            ) {
                Button(
                    onClick = {
                        clearFocus()
                        scope.launch {
                            // Check if droidspaces is in PATH, otherwise use full path
                            val droidspacesCmd = withContext(Dispatchers.IO) {
                                Constants.getDroidspacesCommand()
                            }

                            // Quote container name with double quotes
                            val quotedContainerName = "\"${containerName.replace("\"", "\\\"")}\""
                            val loginCommand = if (selectedUser == "root") {
                                "$droidspacesCmd --name=$quotedContainerName enter"
                            } else {
                                "$droidspacesCmd --name=$quotedContainerName enter $selectedUser"
                            }
                            // Wrap entire command in single quotes for su -c
                            val suCommand = "su -c '$loginCommand'"
                            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                            val clip = ClipData.newPlainText(context.getString(R.string.login_command_copied, selectedUser), suCommand)
                            clipboard.setPrimaryClip(clip)

                            // Show snackbar feedback
                            snackbarHostState?.let { hostState ->
                                scope.launch {
                                    hostState.showSuccess(context.getString(R.string.login_command_copied, selectedUser))
                                }
                            }
                        }
                    },
                    modifier = Modifier.widthIn(min = 140.dp),
                    shape = RoundedCornerShape(16.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        contentColor = MaterialTheme.colorScheme.onPrimary
                    )
                ) {
                    Icon(
                        Icons.Default.ContentCopy,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        context.getString(R.string.copy_login),
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
    }
}

