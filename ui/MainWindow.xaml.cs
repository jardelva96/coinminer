using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using System.Threading.Tasks;
using System.Text;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Diagnostics;

namespace Coinminer.Ui;

public partial class MainWindow : Window
{
    private bool UseLocalWallet => LocalWalletRadio?.IsChecked == true;
    public ObservableCollection<string> ActivityLog { get; } = new();
    public ObservableCollection<DeviceInfo> Devices { get; } = new();

    private readonly DispatcherTimer _uptimeTimer;
    private readonly DispatcherTimer _walletTimer;
    private readonly DispatcherTimer _deviceTimer;
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
    private ulong _lastIdleTicks;
    private ulong _lastKernelTicks;
    private ulong _lastUserTicks;
    private DeviceInfo? _cpuDevice;
    private DeviceInfo? _memoryDevice;

    public MainWindow()
    {
        InitializeComponent();
        SetActiveView("Miner");
        DataContext = this;
        LocalWalletRadio.Checked += OnWalletOptionChanged;
        ExternalWalletRadio.Checked += OnWalletOptionChanged;
        WalletAddressBox.IsEnabled = false;

        _uptimeTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _uptimeTimer.Tick += (_, _) => UpdateUptime();

        _walletTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _walletTimer.Tick += (_, _) => LoadWallet();

        _deviceTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _deviceTimer.Tick += (_, _) => UpdateDeviceStats();
        LoadDevices();
        _deviceTimer.Start();
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
        DocsView.Visibility = Visibility.Collapsed;

        SetNavButtonActive(NavMiner, viewKey == "Miner");
        SetNavButtonActive(NavStats, viewKey == "Stats");
        SetNavButtonActive(NavDevices, viewKey == "Devices");
        SetNavButtonActive(NavTransactions, viewKey == "Transactions");
        SetNavButtonActive(NavSupport, viewKey == "Support");
    }

    private void OnOpenDocsClick(object sender, RoutedEventArgs e)
    {
        SupportView.Visibility = Visibility.Collapsed;
        DocsView.Visibility = Visibility.Visible;
    }

    private void OnBackToSupportClick(object sender, RoutedEventArgs e)
    {
        DocsView.Visibility = Visibility.Collapsed;
        SupportView.Visibility = Visibility.Visible;
    }

    private void OnCopyDocsLinkClick(object sender, RoutedEventArgs e)
    {
        const string docsUrl = "https://solocoinminer.local/docs";
        Clipboard.SetText(docsUrl);
        AddActivity("Link de documentacao copiado.");
    }

    private void OnOpenTelegramClick(object sender, RoutedEventArgs e)
    {
        OpenUrl("https://t.me/solocoinminer");
    }

    private void OnOpenDiscordClick(object sender, RoutedEventArgs e)
    {
        OpenUrl("https://discord.gg/solocoinminer");
    }

    private void OnOpenWebsiteClick(object sender, RoutedEventArgs e)
    {
        OpenUrl("https://solocoinminer.local");
    }

    private void OpenUrl(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = url,
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            AddActivity($"Falha ao abrir link: {ex.Message}");
        }
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

    private async void OnCreateWalletClick(object sender, RoutedEventArgs e)
    {
        var repoRoot = FindRepoRoot();
        _walletPath = Path.Combine(repoRoot, "wallet.dat");

        try
        {
            if (!TryGetRpcSettings(out var host, out var port, out var user, out var pass, out var error))
            {
                WalletStatusValue.Text = error;
                return;
            }

            var address = await RpcCallForStringAsync("getnewaddress", Array.Empty<object>(), host, port, user, pass);
            SaveWalletFile(address);
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
            SaveWalletFile(address);
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

    private void OnWalletOptionChanged(object sender, RoutedEventArgs e)
    {
        if (WalletAddressBox == null) return;
        WalletAddressBox.IsEnabled = ExternalWalletRadio.IsChecked == true;
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
        var minerPath = FindMinerPath(repoRoot);
        if (minerPath == null)
        {
            SetStatus("Miner not found", "Build the C miner first");
            AddActivity("Miner executable not found. Build the C project first.");
            MinerPathValue.Text = "Miner: not found";
            StartStopButton.IsEnabled = true;
            ApplySettingsButton.IsEnabled = true;
            return;
        }

        _walletPath = Path.Combine(repoRoot, "wallet.dat");
        string payoutAddress = null;
        if (UseLocalWallet)
        {
            payoutAddress = LoadLocalWalletAddress();
        }
        else
        {
            payoutAddress = (WalletAddressBox.Text ?? string.Empty).Trim();
        }
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
        BalanceFiat.Text = $"{_coinLabel} saldo";
        StatusValue.Text = "Iniciando";

        _isStratumMode = GetSelectedMode() == "stratum";
        var isSoloMode = GetSelectedMode() == "solo";
        // Se for stratum, usar payoutAddress como user se não estiver preenchido
        if (_isStratumMode)
        {
            if (!IsStratumCoinSupported(coinData))
            {
                SetStatus("Moeda nao suportada", "Selecione Bitcoin, Litecoin ou Dogecoin");
                StratumStatusValue.Text = "Nao conectado";
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
                SetStatus("Pool ausente", "Preencha host e porta");
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }

            if (string.IsNullOrWhiteSpace(user))
            {
                user = string.IsNullOrWhiteSpace(payoutAddress) ? "worker" : payoutAddress;
                PoolUserBox.Text = user;
            }

            var addressPart = ExtractAddressPart(user);
            if (!string.IsNullOrWhiteSpace(addressPart) && !IsValidAddressForCoin(coinData, addressPart))
            {
                SetStatus("Endereco invalido", "Usuario da pool deve ser um endereco valido");
                StratumStatusValue.Text = "Nao conectado";
                StartStopButton.IsEnabled = true;
                ApplySettingsButton.IsEnabled = true;
                return;
            }
            if (string.IsNullOrWhiteSpace(pass))
            {
                pass = "x";
            }

            StatusDetailValue.Text = $"Conectando: {host}:{port}";
            StratumStatusValue.Text = "Conectando...";
            StratumSubmitValue.Text = "Envios: 0 / 0";
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
                SetStatus("No ausente", "Preencha host e porta");
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

            StatusDetailValue.Text = $"Conectando no: {host}:{port}";
            ProfileValue.Text = "Solo node session";
            StratumStatusValue.Text = "Node connecting";
            SoloStatusValue.Text = "Solo: conectando";

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

        StratumStatusValue.Text = "Nao conectado";
        SoloStatusValue.Text = "Solo: ocioso";
        StatusDetailValue.Text = $"Iniciando: {coinData} @ diff {difficulty}";

        var localStartInfo = new ProcessStartInfo
        {
            FileName = minerPath,
            Arguments = $"run \"{coinData}\" {difficulty} 0 --progress {progressInterval} --wallet \"{(UseLocalWallet ? _walletPath : payoutAddress)}\"",
            WorkingDirectory = repoRoot,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        StartProcess(localStartInfo, $"Mining {_coinLabel} (demo)");

    }

    private string LoadLocalWalletAddress()
    {
        // Simples: lê o endereço salvo no arquivo wallet.dat (ajuste conforme seu formato)
        try
        {
            if (File.Exists(_walletPath))
            {
                var lines = File.ReadAllLines(_walletPath);
                return lines.Length > 0 ? lines[0].Trim() : string.Empty;
            }
        }
        catch { }
        return string.Empty;
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
        StratumStatusValue.Text = "Nao conectado";
        SoloStatusValue.Text = "Solo: ocioso";
        StartStopButton.Content = "Iniciar mineracao";
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
                StartStopButton.Content = "Iniciar mineracao";
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
            StartStopButton.Content = "Parar mineracao";
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
                    SoloStatusValue.Text = "Solo: minerando";
                }
                var attempts = ExtractFirstUlong(line);
                if (attempts.HasValue)
                {
                    _lastAttempts = attempts.Value;
                    AttemptsValue.Text = $"{_lastAttempts:N0} tentativas";
                    StatsAttemptsValue.Text = $"{_lastAttempts:N0} tentativas";
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
                    StratumStatusValue.Text = "Conectado";
                }
                if (line.Contains("conexao encerrada", StringComparison.OrdinalIgnoreCase))
                {
                    StratumStatusValue.Text = "Desconectado";
                }
                if (line.Contains("reconectando", StringComparison.OrdinalIgnoreCase))
                {
                    StratumStatusValue.Text = "Reconectando";
                }
                if (line.Contains("difficulty set to", StringComparison.OrdinalIgnoreCase))
                {
                    var diff = ExtractFirstDouble(line);
                    if (diff.HasValue)
                    {
                        _stratumDifficulty = diff.Value;
                        StratumDifficultyValue.Text = $"Dificuldade: {_stratumDifficulty:0.######}";
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
                    SoloStatusValue.Text = "Solo: conectado";
                }
                if (line.Contains("mining job", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: minerando";
                }
                if (line.Contains("submitblock enviado", StringComparison.OrdinalIgnoreCase))
                {
                    SoloStatusValue.Text = "Solo: envio feito";
                }
                return;
            }

            if (line.StartsWith("FOUND!", StringComparison.OrdinalIgnoreCase))
            {
                _lastBlocks += 1;
                BlocksValue.Text = _lastBlocks.ToString("N0", CultureInfo.InvariantCulture);
                StatsBlocksValue.Text = $"Blocos minerados: {_lastBlocks:N0}";
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
                WalletBalanceValue.Text = $"Saldo: {balance:N0}";
            }

            if (ulong.TryParse(lines[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var blocks))
            {
                _lastBlocks = blocks;
                BlocksValue.Text = _lastBlocks.ToString("N0", CultureInfo.InvariantCulture);
                StatsBlocksValue.Text = $"Blocos minerados: {_lastBlocks:N0}";
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
        BalanceFiat.Text = $"{_coinLabel} saldo";
        WalletBalanceValue.Text = $"Saldo: {balance:N0}";
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

    private void SaveWalletFile(string address)
    {
        var contents = $"{address}{Environment.NewLine}0{Environment.NewLine}0{Environment.NewLine}";
        File.WriteAllText(_walletPath, contents);
        WalletAddressBox.Text = address;
        WalletBalanceValue.Text = "Saldo: 0";
        WalletStatusValue.Text = $"Wallet saved: {_walletPath}";
        PoolUserBox.Text = address;
    }

    private bool TryGetRpcSettings(out string host, out int port, out string user, out string pass, out string error)
    {
        host = string.Empty;
        user = "rpcuser";
        pass = "rpcpass";
        error = string.Empty;
        port = 0;

        var hostText = PoolHostBox.Text?.Trim() ?? string.Empty;
        var portText = PoolPortBox.Text?.Trim() ?? string.Empty;
        var userText = PoolUserBox.Text?.Trim() ?? string.Empty;
        var passText = PoolPassBox.Text?.Trim() ?? string.Empty;

        if (string.IsNullOrWhiteSpace(hostText))
        {
            error = "RPC host is empty";
            return false;
        }

        if (!int.TryParse(portText, NumberStyles.Integer, CultureInfo.InvariantCulture, out port) || port <= 0)
        {
            error = "RPC port is invalid";
            return false;
        }

        if (!string.IsNullOrWhiteSpace(userText))
        {
            user = userText;
        }

        if (!string.IsNullOrWhiteSpace(passText))
        {
            pass = passText;
        }

        host = hostText;
        return true;
    }

    private static async Task<string> RpcCallForStringAsync(string method, object[] parameters, string host, int port, string user, string pass)
    {
        var payload = JsonSerializer.Serialize(new
        {
            jsonrpc = "1.0",
            id = "solocoinminer-ui",
            method,
            @params = parameters
        });

        using var client = new HttpClient();
        using var request = new HttpRequestMessage(HttpMethod.Post, new Uri($"http://{host}:{port}/"));
        request.Content = new StringContent(payload, Encoding.UTF8, "application/json");
        var authBytes = Encoding.ASCII.GetBytes($"{user}:{pass}");
        request.Headers.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(authBytes));

        using var response = await client.SendAsync(request);
        var body = await response.Content.ReadAsStringAsync();

        if (!response.IsSuccessStatusCode)
        {
            throw new InvalidOperationException($"RPC HTTP {(int)response.StatusCode}: {body}");
        }

        using var doc = JsonDocument.Parse(body);
        if (doc.RootElement.TryGetProperty("error", out var error) && error.ValueKind != JsonValueKind.Null)
        {
            throw new InvalidOperationException($"RPC error: {error}");
        }

        if (!doc.RootElement.TryGetProperty("result", out var result) || result.ValueKind != JsonValueKind.String)
        {
            throw new InvalidOperationException("RPC result missing or not a string.");
        }

        return result.GetString() ?? string.Empty;
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
                    StratumNotifyValue.Text = $"Notificacoes: {_stratumNotifyCount:N0}";
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

        StratumBytesValue.Text = $"Trafego: {_stratumBytesIn:N0} / {_stratumBytesOut:N0}";
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
                    StratumSubmitValue.Text = $"Envios: {value.Value:N0} / {ExtractRejectCount(line):N0}";
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

    private void LoadDevices()
    {
        Devices.Clear();

        _cpuDevice = new DeviceInfo
        {
            Title = $"CPU - {GetCpuName()}",
            Detail = $"Uso: --% | Threads: {Environment.ProcessorCount}"
        };
        Devices.Add(_cpuDevice);

        _memoryDevice = new DeviceInfo
        {
            Title = "Memoria RAM",
            Detail = "Uso: --"
        };
        Devices.Add(_memoryDevice);

        foreach (var gpu in GetGpuDevices())
        {
            Devices.Add(new DeviceInfo
            {
                Title = $"GPU - {gpu.Name}",
                Detail = $"Estado: {gpu.State}"
            });
        }
    }

    private void UpdateDeviceStats()
    {
        if (_cpuDevice != null)
        {
            var usage = GetCpuUsagePercent();
            _cpuDevice.Detail = $"Uso: {usage:0}% | Threads: {Environment.ProcessorCount}";
            if (StatsCpuValue != null)
            {
                StatsCpuValue.Text = $"CPU: {usage:0}%";
            }
        }

        if (_memoryDevice != null)
        {
            if (TryGetMemoryStatus(out var total, out var available))
            {
                var used = total - available;
                _memoryDevice.Detail = $"Uso: {FormatBytes(used)} / {FormatBytes(total)}";
                if (StatsMemoryValue != null)
                {
                    StatsMemoryValue.Text = $"RAM: {FormatBytes(used)} / {FormatBytes(total)}";
                }
            }
        }

        if (StatsSystemUptimeValue != null)
        {
            var uptime = TimeSpan.FromMilliseconds(Environment.TickCount64);
            StatsSystemUptimeValue.Text = $"Tempo ligado: {FormatDuration(uptime)}";
        }

        if (StatsOsValue != null)
        {
            StatsOsValue.Text = $"SO: {RuntimeInformation.OSDescription}";
        }

        if (StatsMachineValue != null)
        {
            StatsMachineValue.Text = $"Maquina: {Environment.MachineName}";
        }

        UpdateStatsSummary();
    }

    private void UpdateStatsSummary()
    {
        if (StatsModeValue == null || StatsCoinValue == null || StatsPoolValue == null ||
            StatsUserValue == null || StatsWalletValue == null || StatsMinerValue == null)
        {
            return;
        }

        StatsModeValue.Text = GetSelectedModeLabel();
        StatsCoinValue.Text = GetSelectedCoinLabel();

        var host = PoolHostBox.Text?.Trim() ?? string.Empty;
        var port = PoolPortBox.Text?.Trim() ?? string.Empty;
        StatsPoolValue.Text = string.IsNullOrWhiteSpace(host)
            ? "-"
            : (string.IsNullOrWhiteSpace(port) ? host : $"{host}:{port}");

        var user = PoolUserBox.Text?.Trim() ?? string.Empty;
        StatsUserValue.Text = string.IsNullOrWhiteSpace(user) ? "-" : user;

        var wallet = UseLocalWallet ? GetWalletAddress() : (WalletAddressBox.Text ?? string.Empty).Trim();
        StatsWalletValue.Text = string.IsNullOrWhiteSpace(wallet) ? "-" : wallet;

        var minerText = MinerPathValue.Text ?? string.Empty;
        minerText = minerText.Replace("Miner: ", string.Empty, StringComparison.OrdinalIgnoreCase).Trim();
        StatsMinerValue.Text = string.IsNullOrWhiteSpace(minerText) ? "-" : minerText;
    }

    private static string FormatDuration(TimeSpan elapsed)
    {
        if (elapsed.TotalHours >= 1)
        {
            return $"{(int)elapsed.TotalHours}h {elapsed.Minutes}m";
        }

        return $"{elapsed.Minutes}m {elapsed.Seconds}s";
    }

    private string GetSelectedModeLabel()
    {
        if (ModeSelector.SelectedItem is ComboBoxItem item && item.Content is string label)
        {
            return label;
        }
        return "Local";
    }

    private static string GetCpuName()
    {
        var cpu = Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER");
        if (!string.IsNullOrWhiteSpace(cpu))
        {
            return cpu.Trim();
        }

        return "Processador";
    }

    private double GetCpuUsagePercent()
    {
        if (!GetSystemTimes(out var idle, out var kernel, out var user))
        {
            return 0;
        }

        var idleTicks = ToUInt64(idle);
        var kernelTicks = ToUInt64(kernel);
        var userTicks = ToUInt64(user);

        if (_lastKernelTicks == 0)
        {
            _lastIdleTicks = idleTicks;
            _lastKernelTicks = kernelTicks;
            _lastUserTicks = userTicks;
            return 0;
        }

        var idleDelta = idleTicks - _lastIdleTicks;
        var kernelDelta = kernelTicks - _lastKernelTicks;
        var userDelta = userTicks - _lastUserTicks;
        var total = kernelDelta + userDelta;

        _lastIdleTicks = idleTicks;
        _lastKernelTicks = kernelTicks;
        _lastUserTicks = userTicks;

        if (total == 0)
        {
            return 0;
        }

        var usage = (double)(total - idleDelta) / total * 100.0;
        if (usage < 0) usage = 0;
        if (usage > 100) usage = 100;
        return usage;
    }

    private static bool TryGetMemoryStatus(out ulong total, out ulong available)
    {
        var status = new MEMORYSTATUSEX();
        status.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
        if (!GlobalMemoryStatusEx(ref status))
        {
            total = 0;
            available = 0;
            return false;
        }

        total = status.ullTotalPhys;
        available = status.ullAvailPhys;
        return true;
    }

    private static string FormatBytes(ulong bytes)
    {
        const double scale = 1024;
        var value = (double)bytes;
        var units = new[] { "B", "KB", "MB", "GB", "TB" };
        var unitIndex = 0;
        while (value >= scale && unitIndex < units.Length - 1)
        {
            value /= scale;
            unitIndex++;
        }
        return $"{value:0.#} {units[unitIndex]}";
    }

    private static string ToDeviceState(int flags)
    {
        const int DISPLAY_DEVICE_ACTIVE = 0x1;
        return (flags & DISPLAY_DEVICE_ACTIVE) != 0 ? "Ativo" : "Inativo";
    }

    private static IEnumerable<(string Name, string State)> GetGpuDevices()
    {
        var results = new List<(string Name, string State)>();
        var device = new DISPLAY_DEVICE();
        device.cb = Marshal.SizeOf(typeof(DISPLAY_DEVICE));
        uint id = 0;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        while (EnumDisplayDevices(null, id, ref device, 0))
        {
            var name = device.DeviceString?.Trim();
            if (!string.IsNullOrWhiteSpace(name) && seen.Add(name))
            {
                results.Add((name, ToDeviceState(device.StateFlags)));
            }
            id++;
            device.cb = Marshal.SizeOf(typeof(DISPLAY_DEVICE));
        }

        if (results.Count == 0)
        {
            results.Add(("GPU nao detectada", "Inativo"));
        }

        return results;
    }

    private static ulong ToUInt64(FILETIME time)
    {
        return ((ulong)time.dwHighDateTime << 32) | time.dwLowDateTime;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetSystemTimes(out FILETIME idleTime, out FILETIME kernelTime, out FILETIME userTime);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumDisplayDevices(string? lpDevice, uint iDevNum, ref DISPLAY_DEVICE lpDisplayDevice, uint dwFlags);

    [StructLayout(LayoutKind.Sequential)]
    private struct FILETIME
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MEMORYSTATUSEX
    {
        public uint dwLength;
        public uint dwMemoryLoad;
        public ulong ullTotalPhys;
        public ulong ullAvailPhys;
        public ulong ullTotalPageFile;
        public ulong ullAvailPageFile;
        public ulong ullTotalVirtual;
        public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct DISPLAY_DEVICE
    {
        public int cb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string DeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string DeviceString;
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string DeviceID;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string DeviceKey;
    }

    public sealed class DeviceInfo : INotifyPropertyChanged
    {
        private string _title = string.Empty;
        private string _detail = string.Empty;
        private string _secondary = string.Empty;

        public string Title
        {
            get => _title;
            set
            {
                if (_title == value) return;
                _title = value;
                OnPropertyChanged(nameof(Title));
            }
        }

        public string Detail
        {
            get => _detail;
            set
            {
                if (_detail == value) return;
                _detail = value;
                OnPropertyChanged(nameof(Detail));
            }
        }

        public string Secondary
        {
            get => _secondary;
            set
            {
                if (_secondary == value) return;
                _secondary = value;
                OnPropertyChanged(nameof(Secondary));
                OnPropertyChanged(nameof(SecondaryVisibility));
            }
        }

        public Visibility SecondaryVisibility =>
            string.IsNullOrWhiteSpace(_secondary) ? Visibility.Collapsed : Visibility.Visible;

        public event PropertyChangedEventHandler? PropertyChanged;

        private void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
