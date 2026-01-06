# Run Instructions (Codex)

When asked how to run the UI, use:

```powershell
cd ui
dotnet run
```

Notes:
- C miner build errors (e.g., missing headers) are separate from the UI and can be ignored for running the WPF app.
- If build errors mention `MainWindow.xaml.cs`, check for stray/missing braces around class/methods first.
