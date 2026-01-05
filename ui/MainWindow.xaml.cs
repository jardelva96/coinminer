using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using System.Threading.Tasks;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32;

namespace Coinminer.Ui;

public partial class MainWindow : Window
{
    public ObservableCollection<string> ActivityLog { get; } = new();

    private readonly DispatcherTimer _uptimeTimer;
    private readonly DispatcherTimer _walletTimer;
    private Process? _minerProcess;
    private DateTime _miningStartedAt;
    private ulong _lastAttempts;
    private double _lastHashrate;
    private ulong _lastBlocks;
    private string _walletPath = "wallet.dat";
    private string _coinLabel = "Bitcoin";
    private bool _isStopping;
    private DateTime _lastOutputAt;
    private bool _isStratumMode;
    private ulong _stratumNotifyCount;
    private ulong _stratumBytesIn;
    private ulong _stratumBytesOut;
    private double _stratumDifficulty;
    private string _lastWalletAddress = string.Empty;
    private bool _isSimulationMode = true;

    public MainWindow()
    {
        InitializeComponent();
        SetActiveView("Miner");
        DataContext = this;
        UpdateExecutionModeUI();
        PrefillMinerPath();
        UpdateCommandPreview();

        _uptimeTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _uptimeTimer.Tick += (_, _) => UpdateUptime();

        _walletTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _walletTimer.Tick += (_, _) => LoadWallet();
    }

    private void OnClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        _ = StopMiningAsync();
    }

    private void OnNavClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button button && button.Tag is string viewKey)
        {
            SetActiveView(viewKey);
        }
    }

    private void SetActiveView(string viewKey)
    {
        MinerView.Visibility = viewKey == "Miner" ? Visibility.Visible : Visibility.Collapsed;
        StatsView.Visibility = viewKey == "Stats" ? Visibility.Visible : Visibility.Collapsed;
        DevicesView.Visibility = viewKey == "Devices" ? Visibility.Visible : Visibility.Collapsed;
        TransactionsView.Visibility = viewKey == "Transactions" ? Visibility.Visible : Visibility.Collapsed;
        SupportView.Visibility = viewKey == "Support" ? Visibility.Visible : Visibility.Collapsed;

        SetNavButtonActive(NavMiner, viewKey == "Miner");
        SetNavButtonActive(NavStats, viewKey == "Stats");
        SetNavButtonActive(NavDevices, viewKey == "Devices");
        SetNavButtonActive(NavTransactions, viewKey == "Transactions");
        SetNavButtonActive(NavSupport, viewKey == "Support");
    }

    private void SetNavButtonActive(Button button, bool isActive)
    {
        if (button == null)
        {
            return;
        }

        button.Background = isActive
            ? (Brush)FindResource("SidebarActiveBrush")
            : Brushes.Transparent;
        button.Foreground = isActive
            ? (Brush)FindResource("SidebarTextBrush")
            : (Brush)FindResource("SidebarMutedBrush");
    }

    private void OnThemeToggleChanged(object sender, RoutedEventArgs e)
    {
        ApplyTheme(ThemeToggle.IsChecked == true);
    }

    private void UpdateExecutionModeUI()
    {
        if (SimulationModeButton == null || RealModeButton == null)
        {
            return;
        }

        SimulationModeButton.IsChecked = _isSimulationMode;
        RealModeButton.IsChecked = !_isSimulationMode;

        if (ConnectionCardTitle != null)
        {
            ConnectionCardTitle.Text = _isSimulationMode ? "Conexão (pool / simulação)" : "Mineração real (solo RPC)";
        }

        if (ConnectionCardDescription != null)
        {
            ConnectionCardDescription.Text = _isSimulationMode
                ? "Use run/bench ou Stratum/Solo em modo de demonstração."
                : "A UI força modo solo e exige host/porta/usuário/senha do node RPC local.";
        }

        ExecutionModeSummary.Text = _isSimulationMode
            ? "Simulated hashing with local wallet updates."
            : "Real mining via solo RPC (node) using the chosen binary.";

        ModeSelector.IsEnabled = _isSimulationMode;
        RealConfigCallout.Visibility = _isSimulationMode ? Visibility.Collapsed : Visibility.Visible;
        RealModeHint.Visibility = _isSimulationMode ? Visibility.Collapsed : Visibility.Visible;
        ModeLockNote.Visibility = _isSimulationMode ? Visibility.Collapsed : Visibility.Visible;

        if (_isSimulationMode)
        {
            if (ModeSelector.SelectedIndex < 0)
            {
                ModeSelector.SelectedIndex = 0;
            }
            StatusDetailValue.Text = "Modo simulado ativo (run/bench local).";
        }
        else
        {
            ModeSelector.SelectedIndex = 2; // Solo
            StatusDetailValue.Text = "Modo real: configure host/porta/credenciais do node.";
        }

        UpdateCommandPreview();
    }

    private void PrefillMinerPath()
    {
        var repoRoot = FindRepoRoot();
        var detected = FindMinerPath(repoRoot);
        if (!string.IsNullOrWhiteSpace(detected))
        {
            MinerPathBox.Text = detected;
            MinerPathValue.Text = $"Miner: {detected}";
        }
        else
        {
            MinerPathValue.Text = "Miner: not found";
        }

        UpdateCommandPreview();
    }

    private string? GetMinerPath(string repoRoot)
    {
        var fromInput = MinerPathBox.Text?.Trim();
        if (!string.IsNullOrWhiteSpace(fromInput))
        {
            if (File.Exists(fromInput))
            {
                return fromInput;
            }
            StatusDetailValue.Text = "Binário informado não existe. Ajuste o caminho.";
            return null;
        }

        return FindMinerPath(repoRoot);
    }

    private async void OnStartStopClick(object sender, RoutedEventArgs e)
    {
        if (_minerProcess == null || _minerProcess.HasExited)
        {
            StartMining();
        }
        else
        {
            await StopMiningAsync();
        }
    }

    private async void OnApplySettingsClick(object sender, RoutedEventArgs e)
    {
        if (_minerProcess != null && !_minerProcess.HasExited)
        {
            await StopMiningAsync();
        }
        StartMining();
    }

    private void OnSimulationModeClick(object sender, RoutedEventArgs e)
    {
        _isSimulationMode = true;
        UpdateExecutionModeUI();
    }

    private void OnRealModeClick(object sender, RoutedEventArgs e)
    {
        _isSimulationMode = false;
        UpdateExecutionModeUI();
    }

    private void OnModeChanged(object sender, SelectionChangedEventArgs e)
    {
        UpdateCommandPreview();
    }

    private void OnCoinChanged(object sender, SelectionChangedEventArgs e)
    {
        _coinLabel = GetSelectedCoinLabel();
        UpdateCommandPreview();
    }

    private void OnConnectionFieldChanged(object sender, TextChangedEventArgs e)
    {
        UpdateCommandPreview();
    }

    private void OnMiningTuningChanged(object sender, TextChangedEventArgs e)
    {
        UpdateCommandPreview();
    }

    private void OnBrowseMinerPathClick(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "coinminer|coinminer.exe;coinminer|Executables|*.exe;*.bin;*.app|All files|*.*",
            Title = "Selecione o binário do minerador"
        };

        if (dialog.ShowDialog() == true)
        {
            MinerPathBox.Text = dialog.FileName;
            MinerPathValue.Text = $"Miner: {dialog.FileName}";
        }

        UpdateCommandPreview();
    }

    private void OnUseDetectedMinerPathClick(object sender, RoutedEventArgs e)
    {
        var repoRoot = FindRepoRoot();
        var detected = FindMinerPath(repoRoot);
        if (detected != null)
        {
            MinerPathBox.Text = detected;
            MinerPathValue.Text = $"Miner: {detected}";
            StatusDetailValue.Text = "Binário encontrado na pasta build.";
        }
        else
        {
            MinerPathValue.Text = "Miner: not found";
            StatusDetailValue.Text = "Construa o binário ou selecione manualmente.";
        }

        UpdateCommandPreview();
    }

    private void OnMinerPathChanged(object sender, TextChangedEventArgs e)
    {
        UpdateCommandPreview();
    }

    private void OnCreateWalletClick(object sender, RoutedEventArgs e)
    {
        var repoRoot = FindRepoRoot();
        _walletPath = Path.Combine(repoRoot, "wallet.dat");

        try
        {
            var address = GenerateAddress();
            var contents = $"{address}{Environment.NewLine}0{Environment.NewLine}0{Environment.NewLine}";
            File.WriteAllText(_walletPath, contents);
            WalletAddressBox.Text = address;
            WalletBalanceValue.Text = "Balance: 0";
            WalletStatusValue.Text = $"Wallet saved: {_walletPath}";
            PoolUserBox.Text = address;
            AddActivity("Wallet created.");
        }
        catch (Exception ex)
        {
            WalletStatusValue.Text = $"Failed to create wallet: {ex.Message}";
        }
    }

    private void OnOpenWalletClick(object sender, RoutedEventArgs e)
    {
        var repoRoot = FindRepoRoot();
        _walletPath = Path.Combine(repoRoot, "wallet.dat");
        LoadWallet();
    }

    private void OnSaveWalletClick(object sender, RoutedEventArgs e)
    {
        var repoRoot = FindRepoRoot();
        _walletPath = Path.Combine(repoRoot, "wallet.dat");
        var address = (WalletAddressBox.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(address))
        {
            WalletStatusValue.Text = "Wallet address is empty";
            return;
        }

        try
        {
            var contents = $"{address}{Environment.NewLine}0{Environment.NewLine}0{Environment.NewLine}";
            File.WriteAllText(_walletPath, contents);
            WalletStatusValue.Text = $"Wallet saved: {_walletPath}";
            PoolUserBox.Text = address;
            AddActivity("Wallet saved.");
        }
        catch (Exception ex)
        {
            WalletStatusValue.Text = $"Failed to save wallet: {ex.Message}";
        }
    }

    private void OnUseWalletForPoolClick(object sender, RoutedEventArgs e)
    {
        var address = (WalletAddressBox.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(address))
        {
            WalletStatusValue.Text = "Wallet address is empty";
            return;
        }

        PoolUserBox.Text = address;
        WalletStatusValue.Text = "Wallet address applied to pool user";
    }

    private void OnSwitchToStratumClick(object sender, RoutedEventArgs e)
    {
        if (ModeSelector.SelectedItem is ComboBoxItem)
        {
            ModeSelector.SelectedIndex = 1;
        }
    }

    private void OnWalletAddressChanged(object sender, TextChangedEventArgs e)
    {
        var current = (WalletAddressBox.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(current))
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(PoolUserBox.Text) || PoolUserBox.Text == _lastWalletAddress)
        {
            PoolUserBox.Text = current;
        }
        _lastWalletAddress = current;
    }

    private static void ApplyTheme(bool isDark)
    {
        var resources = Application.Current.Resources;
        var themeSource = new Uri(isDark ? "Themes/Dark.xaml" : "Themes/Light.xaml", UriKind.Relative);
        var themeDictionary = resources.MergedDictionaries.FirstOrDefault(dictionary =>
            dictionary.Source != null && dictionary.Source.OriginalString.Contains("Themes/", StringComparison.OrdinalIgnoreCase));

        if (themeDictionary == null)
        {
            resources.MergedDictionaries.Insert(0, new ResourceDictionary { Source = themeSource });
            return;
        }

        if (themeDictionary.Source == null || !themeDictionary.Source.Equals(themeSource))
        {
            themeDictionary.Source = themeSource;
        }
    }

    private void StartMining()
    {
        if (_minerProcess != null && !_minerProcess.HasExited)
        {
            return;
        }

        StartStopButton.IsEnabled = false;
        ApplySettingsButton.IsEnabled = false;

        var repoRoot = FindRepoRoot();
        var minerPath = GetMinerPath(repoRoot);
        if (minerPath == null)
        {
            SetStatus("Miner not found", "Selecione o binário ou construa o projeto (build).");
            AddActivity("Miner executable not found. Select the binary or build the C project first.");
            MinerPathValue.Text = "Miner: not found";
            StartStopButton.IsEnabled = true;
            ApplySettingsButton.IsEnabled = true;
            return;
        }

        _walletPath = Path.Combine(repoRoot, "wallet.dat");
        MinerPathValue.Text = $"Miner: {minerPath}";
        _miningStartedAt = DateTime.UtcNow;
        _lastAttempts = 0;
        _lastHashrate = 0;
        _lastBlocks = 0;
        LastOutputValue.Text = "Last output: -";

        var difficulty = ParseIntOrDefault(DifficultyBox.Text, 4);
        if (difficulty < 0) difficulty = 0;
        if (difficulty > 64) difficulty = 64;

        var progressInterval = ParseUlongOrDefault(ProgressBox.Text, 10000);
        if (progressInterval < 1) progressInterval = 10000;

        _coinLabel = GetSelectedCoinLabel();
        var coinData = GetSelectedCoinData();
        ProfileDetailValue.Text = $"Coin: {_coinLabel}";
        BalanceFiat.Text = $"{_coinLabel} balance";
        StatusValue.Text = "Starting";

        var selectedMode = _isSimulationMode ? GetSelectedMode() : "solo";
        _isStratumMode = selectedMode == "stratum";
        var isSoloMode = selectedMode == "solo";
        if (_isStratumMode)
        {
            if (!IsStratumCoinSupported(coinData))
            {
                SetStatus("Unsupported coin", "Select Bitcoin, Litecoin, or Dogecoin");
                StratumStatusValue.Text = "Not connected";
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }

            var host = PoolHostBox.Text?.Trim() ?? string.Empty;
            var port = PoolPortBox.Text?.Trim() ?? string.Empty;
            var user = PoolUserBox.Text?.Trim();
            var pass = PoolPassBox.Text?.Trim();

            if (string.IsNullOrWhiteSpace(host) || string.IsNullOrWhiteSpace(port))
            {
                SetStatus("Missing pool", "Fill pool host and port");
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }

            if (string.IsNullOrWhiteSpace(user))
            {
                var walletAddress = GetWalletAddress();
                user = string.IsNullOrWhiteSpace(walletAddress) ? "worker" : walletAddress;
                PoolUserBox.Text = user;
            }

            var addressPart = ExtractAddressPart(user);
            if (!string.IsNullOrWhiteSpace(addressPart) && !IsValidAddressForCoin(coinData, addressPart))
            {
                SetStatus("Invalid address", "Pool user must be a valid wallet address");
                StratumStatusValue.Text = "Not connected";
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }
            if (string.IsNullOrWhiteSpace(pass))
            {
                pass = "x";
            }

            StatusDetailValue.Text = $"Connecting: {host}:{port}";
            StratumStatusValue.Text = "Connecting...";
            StratumSubmitValue.Text = "Submit: 0 / 0";
            _stratumNotifyCount = 0;
            _stratumBytesIn = 0;
            _stratumBytesOut = 0;
            _stratumDifficulty = 0;

            HashrateValue.Text = "-";
            AttemptsValue.Text = "-";
            StatsHashrateValue.Text = "-";
            StatsAttemptsValue.Text = "-";

            ProfileValue.Text = "Stratum session";

            var startInfo = new ProcessStartInfo
            {
                FileName = minerPath,
                Arguments = $"stratum {host} {port} {user} {pass} --coin {coinData}",
                WorkingDirectory = repoRoot,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            StartProcess(startInfo, $"Stratum {host}:{port}");
            return;
        }

        if (isSoloMode)
        {
            var host = PoolHostBox.Text?.Trim() ?? string.Empty;
            var port = PoolPortBox.Text?.Trim() ?? string.Empty;
            var user = PoolUserBox.Text?.Trim();
            var pass = PoolPassBox.Text?.Trim();

            if (string.IsNullOrWhiteSpace(host) || string.IsNullOrWhiteSpace(port))
            {
                SetStatus("Missing node", "Fill host and port");
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }

            if (string.IsNullOrWhiteSpace(user))
            {
                user = "rpcuser";
            }
            if (string.IsNullOrWhiteSpace(pass))
            {
                pass = "rpcpass";
            }

            StatusDetailValue.Text = $"Connecting node: {host}:{port}" + (_isSimulationMode ? string.Empty : " (real)");
            ProfileValue.Text = _isSimulationMode ? "Solo node session" : "Real solo node session";
            StratumStatusValue.Text = "Node connecting";
            SoloStatusValue.Text = "Solo: connecting";

            var startInfo = new ProcessStartInfo
            {
                FileName = minerPath,
                Arguments = $"solo {host} {port} {user} {pass} --coin {coinData}",
                WorkingDirectory = repoRoot,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            StartProcess(startInfo, $"Solo {host}:{port}");
            return;
        }

        StratumStatusValue.Text = "Not connected";
        SoloStatusValue.Text = "Solo: idle";
        StatusDetailValue.Text = $"Starting: {coinData} @ diff {difficulty}";

        var localStartInfo = new ProcessStartInfo
        {
            FileName = minerPath,
            Arguments = $"run \"{coinData}\" {difficulty} 0 --progress {progressInterval} --wallet \"{_walletPath}\"",
            WorkingDirectory = repoRoot,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        StartProcess(localStartInfo, $"Mining {_coinLabel} (demo)");
    }

    private void UpdateCommandPreview()
    {
        if (CommandPreviewValue == null)
        {
            return;
        }

        CommandPreviewValue.Text = BuildCommandPreview();
        UpdateConnectionHint();
    }

    private string BuildCommandPreview()
    {
        var minerPath = GetPreviewMinerPath();
        var coinData = GetSelectedCoinData();
        var mode = _isSimulationMode ? GetSelectedMode() : "solo";
        var difficulty = ParseIntOrDefault(DifficultyBox?.Text, 4);
        var progressInterval = ParseUlongOrDefault(ProgressBox?.Text, 10000);
        var host = SafeTrim(PoolHostBox?.Text, "host");
        var port = SafeTrim(PoolPortBox?.Text, "port");
        var user = SafeTrim(PoolUserBox?.Text, "user");
        var pass = SafeTrim(PoolPassBox?.Text, "pass");
        var walletPath = EnsureWalletPreviewPath();

        return mode switch
        {
            "stratum" => $"{minerPath} stratum {host} {port} {user} {pass} --coin {coinData}",
            "solo" => $"{minerPath} solo {host} {port} {user} {pass} --coin {coinData}",
            _ => $"{minerPath} run \"{coinData}\" {difficulty} 0 --progress {progressInterval} --wallet \"{walletPath}\""
        };
    }

    private string EnsureWalletPreviewPath()
    {
        if (!string.IsNullOrWhiteSpace(_walletPath))
        {
            return _walletPath;
        }

        var repoRoot = FindRepoRoot();
        return Path.Combine(repoRoot, "wallet.dat");
    }

    private static string SafeTrim(string? value, string placeholder)
    {
        var trimmed = (value ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return placeholder;
        }

        return trimmed.Contains(' ') ? $"\"{trimmed}\"" : trimmed;
    }

    private string GetPreviewMinerPath()
    {
        var candidate = (MinerPathBox?.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(candidate))
        {
            return "coinminer";
        }

        return candidate.Contains(' ') ? $"\"{candidate}\"" : candidate;
    }

    private void UpdateConnectionHint()
    {
        if (ConnectionHintValue == null)
        {
            return;
        }

        var mode = _isSimulationMode ? GetSelectedMode() : "solo";
        var missing = new Collection<string>();
        var hostEmpty = string.IsNullOrWhiteSpace(PoolHostBox?.Text);
        var portEmpty = string.IsNullOrWhiteSpace(PoolPortBox?.Text);
        var binaryEmpty = string.IsNullOrWhiteSpace(MinerPathBox?.Text);

        if (!_isSimulationMode)
        {
            if (hostEmpty) missing.Add("host");
            if (portEmpty) missing.Add("porta");
            if (binaryEmpty) missing.Add("binário");

            ConnectionHintValue.Text = missing.Count == 0
                ? "Real: pronto para conectar via solo RPC."
                : $"Real: defina {string.Join(", ", missing)} antes de iniciar.";
            return;
        }

        if (mode == "stratum" || mode == "solo")
        {
            if (hostEmpty) missing.Add("host");
            if (portEmpty) missing.Add("porta");
        }

        ConnectionHintValue.Text = missing.Count == 0
            ? "Simulação: run/bench local ou conexão opcional a pool/node."
            : $"Simulação: preencha {string.Join(", ", missing)} para conectar.";
    }

    private async Task StopMiningAsync()
    {
        if (_isStopping)
        {
            return;
        }
        _isStopping = true;

        _uptimeTimer.Stop();
        _walletTimer.Stop();

        if (_minerProcess == null)
        {
            SetStatus("Stopped", "Miner not running");
            _isStopping = false;
            return;
        }

        SetStatus("Stopping", "Stopping miner...");
        StartStopButton.IsEnabled = false;
        ApplySettingsButton.IsEnabled = false;

        try
        {
            if (!_minerProcess.HasExited)
            {
                try
                {
                    _minerProcess.CloseMainWindow();
                }
                catch
                {
                }

                await Task.Run(() =>
                {
                    if (!_minerProcess.HasExited)
                    {
                        if (!_minerProcess.WaitForExit(1500))
                        {
                            _minerProcess.Kill(true);
                        }
                    }
                    _minerProcess.WaitForExit(1500);
                });
            }
        }
        catch (Exception ex)
        {
            AddActivity($"Failed to stop miner: {ex.Message}");
        }
        finally
        {
            _minerProcess.Dispose();
            _minerProcess = null;
        }

        SetStatus("Stopped", "Miner stopped");
        StratumStatusValue.Text = "Not connected";
        SoloStatusValue.Text = "Solo: idle";
        StartStopButton.Content = "Start mining";
        AddActivity("Mining stopped.");
        StartStopButton.IsEnabled = true;
        ApplySettingsButton.IsEnabled = true;
        _isStopping = false;
    }

    private void StartProcess(ProcessStartInfo startInfo, string statusLabel)
    {
        _minerProcess = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        _minerProcess.OutputDataReceived += (_, args) => HandleMinerOutput(args.Data);
        _minerProcess.ErrorDataReceived += (_, args) => HandleMinerOutput(args.Data);
        _minerProcess.Exited += (_, _) =>
        {
            Dispatcher.Invoke(() =>
            {
                var exitCode = 0;
                try
                {
                    exitCode = _minerProcess?.ExitCode ?? 0;
                }
                catch
                {
                }
                SetStatus("Stopped", $"Process exited (code {exitCode})");
                StartStopButton.Content = "Start mining";
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
            });
        };

        try
        {
            if (!_minerProcess.Start())
            {
                SetStatus("Failed to start", "Process did not launch");
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }

            _minerProcess.BeginOutputReadLine();
            _minerProcess.BeginErrorReadLine();

            SetStatus("Running", statusLabel);
            StartStopButton.Content = "Stop mining";
            AddActivity($"{statusLabel} started.");
            _uptimeTimer.Start();
            if (!_isStratumMode)
            {
                _walletTimer.Start();
            }
            StartStopButton.IsEnabled = true;
            ApplySettingsButton.IsEnabled = true;
        }
        catch (Exception ex)
        {
            SetStatus("Failed to start", ex.Message);
            AddActivity($"Failed to start process: {ex.Message}");
            StartStopButton.IsEnabled = true;
            ApplySettingsButton.IsEnabled = true;
        }
    }

    private void HandleMinerOutput(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        Dispatcher.Invoke(() =>
        {
            _lastOutputAt = DateTime.Now;
            LastOutputValue.Text = $"Last output: {_lastOutputAt:HH:mm:ss}";
            AddActivity(line);

            if (line.StartsWith("[progress]", StringComparison.OrdinalIgnoreCase))
            {
                if (line.Contains("solo", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: mining";
                }
                var attempts = ExtractFirstUlong(line);
                if (attempts.HasValue)
                {
                    _lastAttempts = attempts.Value;
                    AttemptsValue.Text = $"{_lastAttempts:N0} attempts";
                    StatsAttemptsValue.Text = $"{_lastAttempts:N0} attempts";
                }

                var rate = ExtractHashrate(line);
                if (rate.HasValue)
                {
                    _lastHashrate = rate.Value;
                    HashrateValue.Text = $"{_lastHashrate:0.##} H/s";
                    StatsHashrateValue.Text = $"{_lastHashrate:0.##} H/s";
                    ProfileValue.Text = $"CPU: {_lastHashrate:0.##} H/s (SHA-256)";
                }

                return;
            }

            if (line.StartsWith("[stratum]", StringComparison.OrdinalIgnoreCase))
            {
                if (line.Contains("conectado", StringComparison.OrdinalIgnoreCase))
                {
                    StratumStatusValue.Text = "Connected";
                }
                if (line.Contains("conexao encerrada", StringComparison.OrdinalIgnoreCase))
                {
                    StratumStatusValue.Text = "Disconnected";
                }
                if (line.Contains("reconectando", StringComparison.OrdinalIgnoreCase))
                {
                    StratumStatusValue.Text = "Reconnecting";
                }
                if (line.Contains("difficulty set to", StringComparison.OrdinalIgnoreCase))
                {
                    var diff = ExtractFirstDouble(line);
                    if (diff.HasValue)
                    {
                        _stratumDifficulty = diff.Value;
                        StratumDifficultyValue.Text = $"Difficulty: {_stratumDifficulty:0.######}";
                    }
                }
                if (line.StartsWith("[stratum] stats:", StringComparison.OrdinalIgnoreCase))
                {
                    UpdateStratumStats(line);
                }
                if (line.StartsWith("[stratum] submit:", StringComparison.OrdinalIgnoreCase))
                {
                    UpdateStratumSubmits(line);
                }
                return;
            }

            if (line.StartsWith("[solo]", StringComparison.OrdinalIgnoreCase))
            {
                if (line.Contains("conectado", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: connected";
                }
                if (line.Contains("mining job", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: mining";
                }
                if (line.Contains("submitblock enviado", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: submit sent";
                }
                return;
            }

            if (line.StartsWith("FOUND!", StringComparison.OrdinalIgnoreCase))
            {
                _lastBlocks += 1;
                BlocksValue.Text = _lastBlocks.ToString("N0", CultureInfo.InvariantCulture);
                StatsBlocksValue.Text = $"Blocks mined: {_lastBlocks:N0}";
                return;
            }

            if (line.StartsWith("Balance:", StringComparison.OrdinalIgnoreCase))
            {
                var balance = ExtractFirstUlong(line);
                if (balance.HasValue)
                {
                    UpdateBalance(balance.Value);
                }
            }
        });
    }

    private void UpdateUptime()
    {
        if (_miningStartedAt == default)
        {
            return;
        }

        var elapsed = DateTime.UtcNow - _miningStartedAt;
        var text = elapsed.TotalHours >= 1
            ? $"{(int)elapsed.TotalHours}h {elapsed.Minutes}m"
            : $"{elapsed.Minutes}m {elapsed.Seconds}s";

        UptimeValue.Text = $"Uptime: {text}";
        StatsUptimeValue.Text = text;
    }

    private void LoadWallet()
    {
        if (!File.Exists(_walletPath))
        {
            return;
        }

        try
        {
            var lines = File.ReadAllLines(_walletPath);
            if (lines.Length < 3)
            {
                return;
            }

            if (ulong.TryParse(lines[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out var balance))
            {
                UpdateBalance(balance);
                WalletBalanceValue.Text = $"Balance: {balance:N0}";
            }

            if (ulong.TryParse(lines[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var blocks))
            {
                _lastBlocks = blocks;
                BlocksValue.Text = _lastBlocks.ToString("N0", CultureInfo.InvariantCulture);
                StatsBlocksValue.Text = $"Blocks mined: {_lastBlocks:N0}";
            }

            WalletAddressBox.Text = lines[0];
            WalletStatusValue.Text = $"Wallet loaded: {_walletPath}";
            PoolUserBox.Text = lines[0];
        }
        catch
        {
        }
    }

    private void UpdateBalance(ulong balance)
    {
        BalanceValue.Text = balance.ToString("N0", CultureInfo.InvariantCulture);
        BalanceTicker.Text = $"{balance:N0} {_coinLabel.ToUpperInvariant()}";
        BalanceFiat.Text = $"{_coinLabel} balance";
        WalletBalanceValue.Text = $"Balance: {balance:N0}";
    }

    private void SetStatus(string status, string detail)
    {
        StatusValue.Text = status;
        StatusDetailValue.Text = detail;
    }

    private void AddActivity(string message)
    {
        var entry = $"[{DateTime.Now:HH:mm:ss}] {message}";
        ActivityLog.Insert(0, entry);
        if (ActivityLog.Count > 40)
        {
            ActivityLog.RemoveAt(ActivityLog.Count - 1);
        }
    }

    private static string FindRepoRoot()
    {
        var current = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
        while (current != null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt")))
            {
                return current.FullName;
            }
            current = current.Parent;
        }

        return AppDomain.CurrentDomain.BaseDirectory;
    }

    private static string? FindMinerPath(string repoRoot)
    {
        var candidates = new[]
        {
            Path.Combine(repoRoot, "build", "Release", "coinminer.exe"),
            Path.Combine(repoRoot, "build", "Debug", "coinminer.exe"),
            Path.Combine(repoRoot, "build", "coinminer.exe"),
            Path.Combine(repoRoot, "build-ninja", "Release", "coinminer.exe"),
            Path.Combine(repoRoot, "build-ninja", "Debug", "coinminer.exe"),
            Path.Combine(repoRoot, "build-ninja", "coinminer.exe")
        };

        return candidates.FirstOrDefault(File.Exists);
    }

    private static ulong? ExtractFirstUlong(string input)
    {
        ulong value = 0;
        var found = false;

        foreach (var ch in input)
        {
            if (char.IsDigit(ch))
            {
                found = true;
                value = (value * 10) + (ulong)(ch - '0');
            }
            else if (found)
            {
                break;
            }
        }

        return found ? value : null;
    }

    private static double? ExtractHashrate(string input)
    {
        var marker = "H/s";
        var index = input.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
        if (index <= 0)
        {
            return null;
        }

        var numberEnd = index;
        var numberStart = input.LastIndexOf('|', numberEnd);
        if (numberStart < 0)
        {
            return null;
        }

        var segment = input.Substring(numberStart + 1, numberEnd - numberStart - 1);
        segment = segment.Replace("H/s", string.Empty, StringComparison.OrdinalIgnoreCase).Trim();
        segment = segment.Replace(",", ".");

        if (double.TryParse(segment, NumberStyles.Float, CultureInfo.InvariantCulture, out var rate))
        {
            return rate;
        }

        return null;
    }

    private static double? ExtractFirstDouble(string input)
    {
        var sb = new System.Text.StringBuilder();
        var started = false;
        foreach (var ch in input)
        {
            if (char.IsDigit(ch) || ch == '.' || ch == ',')
            {
                sb.Append(ch);
                started = true;
            }
            else if (started)
            {
                break;
            }
        }

        var text = sb.ToString().Replace(",", ".");
        if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
        {
            return value;
        }
        return null;
    }

    private static int ParseIntOrDefault(string? input, int fallback)
    {
        if (int.TryParse(input, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value))
        {
            return value;
        }
        return fallback;
    }

    private static ulong ParseUlongOrDefault(string? input, ulong fallback)
    {
        if (ulong.TryParse(input, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value))
        {
            return value;
        }
        return fallback;
    }

    private static string GenerateAddress()
    {
        Span<byte> data = stackalloc byte[32];
        RandomNumberGenerator.Fill(data);
        var sb = new StringBuilder(64);
        foreach (var b in data)
        {
            sb.Append(b.ToString("x2", CultureInfo.InvariantCulture));
        }
        return sb.ToString();
    }

    private string GetWalletAddress()
    {
        if (!File.Exists(_walletPath))
        {
            return string.Empty;
        }

        try
        {
            var lines = File.ReadAllLines(_walletPath);
            if (lines.Length >= 1)
            {
                return lines[0].Trim();
            }
        }
        catch
        {
        }

        return string.Empty;
    }

    private string GetSelectedCoinLabel()
    {
        if (CoinSelector.SelectedItem is ComboBoxItem item && item.Content is string label)
        {
            return label;
        }
        return "Bitcoin";
    }

    private string GetSelectedCoinData()
    {
        if (CoinSelector.SelectedItem is ComboBoxItem item && item.Tag is string tag)
        {
            return tag;
        }
        return "bitcoin";
    }

    private string GetSelectedMode()
    {
        if (ModeSelector.SelectedItem is ComboBoxItem item && item.Tag is string tag)
        {
            return tag;
        }
        return "local";
    }

    private static bool IsStratumCoinSupported(string coin)
    {
        return coin.Equals("bitcoin", StringComparison.OrdinalIgnoreCase)
            || coin.Equals("litecoin", StringComparison.OrdinalIgnoreCase)
            || coin.Equals("dogecoin", StringComparison.OrdinalIgnoreCase);
    }

    private static string ExtractAddressPart(string user)
    {
        if (string.IsNullOrWhiteSpace(user))
        {
            return string.Empty;
        }

        var dot = user.IndexOf('.');
        if (dot > 0)
        {
            return user.Substring(0, dot);
        }

        return user;
    }

    private static bool IsValidAddressForCoin(string coin, string address)
    {
        if (string.IsNullOrWhiteSpace(address))
        {
            return false;
        }

        if (coin.Equals("bitcoin", StringComparison.OrdinalIgnoreCase))
        {
            return IsBech32Address(address, "bc1") || IsBase58Address(address, new[] { '1', '3' }, 26, 35);
        }

        if (coin.Equals("litecoin", StringComparison.OrdinalIgnoreCase))
        {
            return IsBech32Address(address, "ltc1") || IsBase58Address(address, new[] { 'L', 'M', '3' }, 26, 35);
        }

        if (coin.Equals("dogecoin", StringComparison.OrdinalIgnoreCase))
        {
            return IsBase58Address(address, new[] { 'D' }, 26, 35);
        }

        return false;
    }

    private static bool IsBech32Address(string address, string prefix)
    {
        if (!address.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (address.Length < prefix.Length + 6 || address.Length > 90)
        {
            return false;
        }

        const string charset = "023456789acdefghjklmnpqrstuvwxyz";
        for (int i = prefix.Length; i < address.Length; i++)
        {
            var ch = char.ToLowerInvariant(address[i]);
            if (!charset.Contains(ch))
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsBase58Address(string address, char[] prefixes, int minLen, int maxLen)
    {
        if (address.Length < minLen || address.Length > maxLen)
        {
            return false;
        }

        if (!prefixes.Contains(address[0]))
        {
            return false;
        }

        const string charset = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        foreach (var ch in address)
        {
            if (!charset.Contains(ch))
            {
                return false;
            }
        }

        return true;
    }

    private void UpdateStratumStats(string line)
    {
        var parts = line.Split('|', StringSplitOptions.TrimEntries);
        foreach (var part in parts)
        {
            if (part.Contains("notify=", StringComparison.OrdinalIgnoreCase))
            {
                var value = ExtractFirstUlong(part);
                if (value.HasValue)
                {
                    _stratumNotifyCount = value.Value;
                    StratumNotifyValue.Text = $"Notify: {_stratumNotifyCount:N0}";
                }
            }
            else if (part.Contains("bytes_in=", StringComparison.OrdinalIgnoreCase))
            {
                var value = ExtractFirstUlong(part);
                if (value.HasValue)
                {
                    _stratumBytesIn = value.Value;
                }
            }
            else if (part.Contains("bytes_out=", StringComparison.OrdinalIgnoreCase))
            {
                var value = ExtractFirstUlong(part);
                if (value.HasValue)
                {
                    _stratumBytesOut = value.Value;
                }
            }
        }

        StratumBytesValue.Text = $"Traffic: {_stratumBytesIn:N0} / {_stratumBytesOut:N0}";
    }

    private void UpdateStratumSubmits(string line)
    {
        var parts = line.Split('|', StringSplitOptions.TrimEntries);
        foreach (var part in parts)
        {
            if (part.Contains("accepted=", StringComparison.OrdinalIgnoreCase))
            {
                var value = ExtractFirstUlong(part);
                if (value.HasValue)
                {
                    StratumSubmitValue.Text = $"Submit: {value.Value:N0} / {ExtractRejectCount(line):N0}";
                }
            }
        }
    }

    private static ulong ExtractRejectCount(string line)
    {
        var idx = line.IndexOf("rejected=", StringComparison.OrdinalIgnoreCase);
        if (idx < 0)
        {
            return 0;
        }
        var part = line.Substring(idx);
        var value = ExtractFirstUlong(part);
        return value ?? 0;
    }
}
