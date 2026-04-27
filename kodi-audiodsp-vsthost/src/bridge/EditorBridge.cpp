// Code of EditorBridge.cpp (unchanged portions omitted)

void EditorBridge::uiThreadLoop() {
    // Previous code...

    // Handling messageStatus
    if (messageStatus == -1) {
        // Handling logic...
    }
    // Removed stray "m_running = false; break; }"
    // Keep while(GetMessageW...) loop structure intact
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Loop content...
    }
}

// Rest of the file remains unchanged...