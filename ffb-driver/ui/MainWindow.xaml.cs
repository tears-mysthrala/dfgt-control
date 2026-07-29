using Microsoft.Win32;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace DfgtControl;

public partial class MainWindow : Window
{
    private readonly string profilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DfgtControl", "profiles.json");
    private readonly JsonSerializerOptions jsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };
    private ProfileDocument document = new();
    private int loadedRevision;
    private bool loading;
    private bool agentStartAttempted;
    private bool refreshingStatus;
    private bool dirty;
    private Profile? editingProfile;

    public MainWindow()
    {
        InitializeComponent();
        LoadProfiles();
        _ = RefreshAgentStatusAsync();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += async (_, _) => await RefreshAgentStatusAsync();
        timer.Start();
    }

    private void LoadProfiles()
    {
        if (!File.Exists(profilePath))
        {
            document = new();
            document.Profiles.Add(Profile.Desktop());
            loadedRevision = 0;
            BindProfiles();
            return;
        }
        try
        {
            document = JsonSerializer.Deserialize<ProfileDocument>(
                File.ReadAllText(profilePath), jsonOptions) ?? new();
            if (!ProfileValidator.IsValid(document, out var error))
                throw new InvalidDataException(error);
            loadedRevision = document.Revision;
        }
        catch (JsonException)
        {
            if (!QuarantineConfirmedCorruption())
                SaveStatus.Text = "JSON inválido; no se pudo preservar en cuarentena";
            document = NewDefaultDocument();
            if (SaveStatus.Text.Length == 0)
                SaveStatus.Text = "JSON inválido preservado en cuarentena";
        }
        catch (InvalidDataException error)
        {
            // An unsupported future schema is not corruption.
            if (document.SchemaVersion != 1)
            {
                document = NewDefaultDocument();
                SaveStatus.Text = "Schema no compatible; fichero preservado";
            }
            else
            {
                var quarantined = QuarantineConfirmedCorruption();
                document = NewDefaultDocument();
                SaveStatus.Text = quarantined
                    ? $"Perfiles inválidos preservados: {error.Message}"
                    : $"Perfiles inválidos; fichero intacto: {error.Message}";
            }
        }
        catch (IOException error)
        {
            document = NewDefaultDocument();
            SaveStatus.Text = $"No se pudo leer; fichero preservado: {error.Message}";
        }
        catch (UnauthorizedAccessException)
        {
            document = NewDefaultDocument();
            SaveStatus.Text = "Acceso denegado; fichero preservado";
        }
        BindProfiles();
    }

    private static ProfileDocument NewDefaultDocument()
    {
        var value = new ProfileDocument();
        value.Profiles.Add(Profile.Desktop());
        return value;
    }

    private void BindProfiles()
    {
        ProfilesList.ItemsSource = document.Profiles;
        ProfilesList.SelectedIndex = 0;
    }

    private bool QuarantineConfirmedCorruption()
    {
        try
        {
            var quarantined = profilePath +
                $".invalid-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.json";
            File.Move(profilePath, quarantined);
            return true;
        }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    private async Task RefreshAgentStatusAsync()
    {
        if (refreshingStatus) return;
        refreshingStatus = true;
        try
        {
            var status = await AgentClient.ExchangeAsync(
                AgentCommand.GetStatus);
            var safe = status.SafetyMode is 3 or 4;
            AgentStatus.Text = status.SafetyMode switch
            {
                5 => "PARADA ENCLAVADA",
                6 => "PARADA NO CONFIRMADA · desconecta el volante",
                7 => $"Fallo de seguridad · 0x{status.LastSafetyError:X8}",
                _ when status.Connected && status.FfbClientConnected =>
                    "Volante y juego conectados",
                _ when status.Connected => "Volante preparado",
                _ => "Agente activo · volante desconectado"
            };
            var hvci = ReadHvciState();
            var profile = string.IsNullOrEmpty(status.ActiveProfileId)
                ? "sin perfil activo"
                : $"perfil {status.ActiveProfileId}";
            AgentStatus.Text += $" · {profile} · HVCI {hvci}";
            AgentDot.Fill = new SolidColorBrush(
                (Color)ColorConverter.ConvertFromString(
                    status.SafetyMode == 6 ? "#E96868" :
                    status.SafetyMode == 5 ? "#F2B84B" :
                    safe ? "#35D6A0" : "#66717E"));
            RearmButton.IsEnabled =
                status.SafetyMode == 5 && status.StopConfirmed;
            EmergencyButton.IsEnabled = status.Connected;
            return;
        }
        catch (Exception error) when (
            error is IOException or TimeoutException or
            UnauthorizedAccessException or OperationCanceledException or
            COMException)
        {
            if (!agentStartAttempted)
            {
                agentStartAttempted = true;
                var agent = Path.Combine(
                    AppContext.BaseDirectory, "DfgtAgent.exe");
                if (File.Exists(agent))
                {
                    try
                    {
                        Process.Start(new ProcessStartInfo(agent)
                        {
                            UseShellExecute = false,
                            WorkingDirectory = AppContext.BaseDirectory
                        });
                    }
                    catch { }
                }
            }
            AgentStatus.Text = "Agente no disponible";
            AgentDot.Fill = new SolidColorBrush(
                (Color)ColorConverter.ConvertFromString("#E96868"));
            EmergencyButton.IsEnabled = false;
            RearmButton.IsEnabled = false;
        }
        finally
        {
            refreshingStatus = false;
        }
    }

    private static string ReadHvciState()
    {
        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(
                @"SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\" +
                "HypervisorEnforcedCodeIntegrity");
            return Convert.ToInt32(key?.GetValue("Enabled", -1)) switch
            {
                1 => "configurado",
                0 => "desactivado",
                _ => "desconocido"
            };
        }
        catch (Exception error) when (
            error is IOException or UnauthorizedAccessException or
            System.Security.SecurityException)
        {
            return "desconocido";
        }
    }

    private async void EmergencyStop_OnClick(
        object sender, RoutedEventArgs e)
    {
        try
        {
            var status = await AgentClient.ExchangeAsync(
                AgentCommand.EmergencyStop);
            AgentStatus.Text = status.StopConfirmed
                ? "PARADA ENCLAVADA · fuerza neutral confirmada"
                : "PARADA NO CONFIRMADA · desconecta el volante";
        }
        catch (Exception error)
        {
            AgentStatus.Text =
                $"No se pudo confirmar la parada: {error.Message}";
        }
        await RefreshAgentStatusAsync();
    }

    private async void Rearm_OnClick(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(
                this,
                "El rearme permite que los juegos vuelvan a producir fuerza. " +
                "Confirma que el volante puede moverse con seguridad.",
                "Rearmar force feedback",
                MessageBoxButton.OKCancel,
                MessageBoxImage.Warning) != MessageBoxResult.OK)
            return;
        try
        {
            await AgentClient.ExchangeAsync(
                AgentCommand.AcknowledgeAndRearm);
        }
        catch (Exception error)
        {
            AgentStatus.Text = $"Rearme rechazado: {error.Message}";
        }
        await RefreshAgentStatusAsync();
    }

    private Profile? Selected => ProfilesList.SelectedItem as Profile;

    private void ProfilesList_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (editingProfile is not null && dirty && !loading)
        {
            CopyEditorTo(editingProfile);
            SaveStatus.Text = "Cambios sin guardar conservados en memoria";
        }
        if (Selected is not { } profile) return;
        editingProfile = profile;
        loading = true;
        ProfileName.Text = profile.Name;
        ExecutablePath.Text = profile.ExecutablePath;
        Range.Value = profile.RangeDegrees;
        Overall.Value = profile.OverallGain / 100d;
        Boundary.Value = profile.BoundaryForce / 50d;
        Spring.Value = profile.SpringGain / 100d;
        Damper.Value = profile.DamperGain / 100d;
        Friction.Value = profile.FrictionGain / 100d;
        Periodic.Value = profile.PeriodicGain / 100d;
        loading = false;
        UpdateLabels();
    }

    private void CopyEditorTo(Profile profile)
    {
        profile.Name = string.IsNullOrWhiteSpace(ProfileName.Text)
            ? "Sin nombre" : ProfileName.Text.Trim();
        profile.ExecutablePath = ExecutablePath.Text;
        profile.RangeDegrees = (int)Range.Value;
        profile.OverallGain = (int)Overall.Value * 100;
        profile.BoundaryForce = (int)Boundary.Value * 50;
        profile.SpringGain = (int)Spring.Value * 100;
        profile.DamperGain = (int)Damper.Value * 100;
        profile.FrictionGain = (int)Friction.Value * 100;
        profile.PeriodicGain = (int)Periodic.Value * 100;
    }

    private void Save_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } profile) return;
        CopyEditorTo(profile);
        if (!ProfileValidator.IsValid(document, out var validationError))
        {
            SaveStatus.Text = $"No guardado: {validationError}";
            return;
        }
        try
        {
            if (File.Exists(profilePath))
            {
                var current = JsonSerializer.Deserialize<ProfileDocument>(
                    File.ReadAllText(profilePath), jsonOptions);
                if (current?.Revision != loadedRevision)
                    throw new InvalidOperationException(
                        "el fichero cambió en otra instancia; vuelve a cargar");
            }
            document.Revision = checked(loadedRevision + 1);
            Directory.CreateDirectory(Path.GetDirectoryName(profilePath)!);
            var temporary = profilePath + $".{Guid.NewGuid():N}.tmp";
            var bytes = JsonSerializer.SerializeToUtf8Bytes(document, jsonOptions);
            using (var stream = new FileStream(
                temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                4096, FileOptions.WriteThrough))
            {
                stream.Write(bytes);
                stream.Flush(true);
            }
            if (File.Exists(profilePath))
                File.Replace(temporary, profilePath, profilePath + ".last-good", true);
            else
                File.Move(temporary, profilePath);
            loadedRevision = document.Revision;
            dirty = false;
            ProfilesList.Items.Refresh();
            SaveStatus.Text = "Guardado · pendiente de confirmación del agente";
        }
        catch (Exception error) when (
            error is IOException or UnauthorizedAccessException or
            InvalidOperationException or JsonException or OverflowException)
        {
            SaveStatus.Text = $"No guardado: {error.Message}";
        }
    }

    private void AddProfile_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is { } current) CopyEditorTo(current);
        var profile = Profile.NewGame();
        document.Profiles.Add(profile);
        ProfilesList.Items.Refresh();
        ProfilesList.SelectedItem = profile;
        dirty = true;
    }

    private void DeleteProfile_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } profile || profile.Id == "desktop") return;
        document.Profiles.Remove(profile);
        ProfilesList.Items.Refresh();
        ProfilesList.SelectedIndex = 0;
        dirty = true;
        SaveStatus.Text = "Pulsa «Guardar y aplicar» para confirmar";
    }

    private void BrowseExecutable_OnClick(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Aplicaciones (*.exe)|*.exe",
            CheckFileExists = true,
            Title = "Seleccionar juego"
        };
        if (dialog.ShowDialog(this) == true)
            ExecutablePath.Text = dialog.FileName;
    }

    private void Range_OnValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!loading)
        {
            dirty = true;
            UpdateLabels();
        }
    }

    private void Percent_OnValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!loading)
        {
            dirty = true;
            UpdateLabels();
        }
    }

    private void Editor_OnChanged(object sender, TextChangedEventArgs e)
    {
        if (!loading) dirty = true;
    }

    protected override void OnClosing(
        System.ComponentModel.CancelEventArgs e)
    {
        if (dirty && MessageBox.Show(
                this,
                "Hay cambios sin guardar. ¿Cerrar y descartarlos?",
                "DFGT Control",
                MessageBoxButton.OKCancel,
                MessageBoxImage.Warning) != MessageBoxResult.OK)
            e.Cancel = true;
        base.OnClosing(e);
    }

    private void UpdateLabels()
    {
        if (!IsInitialized ||
            RangeValue is null ||
            OverallValue is null ||
            BoundaryValue is null ||
            SpringValue is null ||
            DamperValue is null ||
            FrictionValue is null ||
            PeriodicValue is null) return;
        RangeValue.Text = $"{(int)Range.Value}°";
        OverallValue.Text = $"{(int)Overall.Value}%";
        BoundaryValue.Text = $"{(int)Boundary.Value}%";
        SpringValue.Text = $"{(int)Spring.Value}%";
        DamperValue.Text = $"{(int)Damper.Value}%";
        FrictionValue.Text = $"{(int)Friction.Value}%";
        PeriodicValue.Text = $"{(int)Periodic.Value}%";
    }
}

public sealed class ProfileDocument
{
    public int SchemaVersion { get; set; } = 1;
    public int Revision { get; set; }
    public List<Profile> Profiles { get; set; } = [];
}

public sealed class Profile
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string ExecutablePath { get; set; } = "";
    public int RangeDegrees { get; set; } = 900;
    public int OverallGain { get; set; } = 10000;
    public int BoundaryForce { get; set; } = 3000;
    public int SpringGain { get; set; } = 10000;
    public int DamperGain { get; set; } = 10000;
    public int FrictionGain { get; set; } = 10000;
    public int PeriodicGain { get; set; } = 10000;

    public static Profile Desktop() => new() { Id = "desktop", Name = "Escritorio" };
    public static Profile NewGame() => new()
    {
        Id = $"game-{Guid.NewGuid():N}"[..21],
        Name = "Nuevo juego",
        RangeDegrees = 540
    };
}

internal static class ProfileValidator
{
    public static bool IsValid(ProfileDocument? document, out string error)
    {
        error = "";
        if (document is null) return Fail("documento vacío", out error);
        if (document.SchemaVersion != 1)
            return Fail("schema no compatible", out error);
        if (document.Revision < 0)
            return Fail("revision inválida", out error);
        if (document.Profiles is null ||
            document.Profiles.Count is < 1 or > 256)
            return Fail("cantidad de perfiles inválida", out error);
        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var desktops = 0;
        foreach (var profile in document.Profiles)
        {
            if (profile.Id.Length is < 1 or >= 64 ||
                profile.Id.Any(character =>
                    !char.IsAsciiLetterOrDigit(character) &&
                    character is not '.' and not '_' and not '-'))
                return Fail("ID inválido", out error);
            if (!ids.Add(profile.Id)) return Fail("ID duplicado", out error);
            if (profile.Name.Length is < 1 or > 128)
                return Fail("nombre inválido", out error);
            if (profile.Id.Equals("desktop", StringComparison.OrdinalIgnoreCase))
            {
                desktops++;
                if (profile.ExecutablePath.Length != 0)
                    return Fail("desktop no admite ejecutable", out error);
            }
            else if (profile.ExecutablePath.Length != 0)
            {
                if (!Path.IsPathFullyQualified(profile.ExecutablePath))
                    return Fail("ruta no absoluta", out error);
                string normalized;
                try
                {
                    normalized = Path.GetFullPath(profile.ExecutablePath);
                }
                catch (Exception pathError) when (
                    pathError is ArgumentException or NotSupportedException or
                    PathTooLongException)
                {
                    return Fail("ruta inválida", out error);
                }
                if (!paths.Add(normalized))
                    return Fail("ejecutable duplicado", out error);
            }
            if (profile.RangeDegrees is < 40 or > 900 ||
                !Gain(profile.OverallGain) ||
                profile.BoundaryForce is < 0 or > 5000 ||
                !Gain(profile.SpringGain) || !Gain(profile.DamperGain) ||
                !Gain(profile.FrictionGain) || !Gain(profile.PeriodicGain))
                return Fail("parámetros fuera de rango", out error);
        }
        return desktops == 1 || Fail("se requiere un único desktop", out error);
    }

    private static bool Gain(int value) => value is >= 0 and <= 10000;
    private static bool Fail(string value, out string error)
    {
        error = value;
        return false;
    }
}
