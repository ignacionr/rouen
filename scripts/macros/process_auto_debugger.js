// scripts/macros/process_auto_debugger.js

Rouen.log("[QuickJS Macro] Process Crash Debugger initialized");

Rouen.on("host:process:exited", (evt) => {
    const payload = evt.payload;
    if (payload && payload.exit_code !== 0) {
        Rouen.log(`[ALERT] Process ${payload.command_line} exited with code ${payload.exit_code}`);
        const promptText = `Process '${payload.command_line}' crashed with exit code ${payload.exit_code}.\n` +
                           `Please analyze the crash reason.`;
        Rouen.cards.create("ai-chat", { prompt: promptText });
    }
});
