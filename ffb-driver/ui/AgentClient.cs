using Microsoft.Win32.SafeHandles;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;

namespace DfgtControl;

internal enum AgentCommand : uint
{
    GetStatus = 1,
    EmergencyStop = 2,
    AcknowledgeAndRearm = 3
}

internal sealed record AgentStatus(
    int Result,
    uint DeviceState,
    bool Connected,
    bool FfbClientConnected,
    uint SafetyMode,
    uint ActiveSlots,
    bool StopConfirmed,
    int LastSafetyError,
    ulong SafetyGeneration,
    string ActiveProfileId);

internal static class AgentClient
{
    private const uint Magic = 0x55474644;
    private const ushort Version = 1;
    private const int RequestSize = 16;
    private const int ResponseSize = 184;
    private static uint sequence;

    public static async Task<AgentStatus> ExchangeAsync(
        AgentCommand command,
        CancellationToken cancellationToken = default)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        timeout.CancelAfter(TimeSpan.FromMilliseconds(700));
        await using var pipe = new NamedPipeClientStream(
            ".", "DfgtControl.Agent.v1",
            PipeDirection.InOut, PipeOptions.Asynchronous);
        await pipe.ConnectAsync(500, timeout.Token);
        ValidateServer(pipe.SafePipeHandle);

        var request = new byte[RequestSize];
        BinaryPrimitives.WriteUInt32LittleEndian(request, Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(request.AsSpan(4), Version);
        BinaryPrimitives.WriteUInt16LittleEndian(request.AsSpan(6), RequestSize);
        var requestSequence = unchecked(++sequence);
        BinaryPrimitives.WriteUInt32LittleEndian(
            request.AsSpan(8), (uint)command);
        BinaryPrimitives.WriteUInt32LittleEndian(
            request.AsSpan(12), requestSequence);
        await pipe.WriteAsync(request, timeout.Token);

        var response = new byte[ResponseSize];
        await pipe.ReadExactlyAsync(response, timeout.Token);
        if (BinaryPrimitives.ReadUInt32LittleEndian(response) != Magic ||
            BinaryPrimitives.ReadUInt16LittleEndian(response.AsSpan(4)) != Version ||
            BinaryPrimitives.ReadUInt16LittleEndian(response.AsSpan(6)) !=
                ResponseSize ||
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(8)) !=
                requestSequence)
            throw new InvalidDataException("Respuesta IPC del agente inválida.");
        var acknowledgement = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(
            acknowledgement, requestSequence);
        await pipe.WriteAsync(acknowledgement, timeout.Token);
        await pipe.FlushAsync(timeout.Token);

        var status = new AgentStatus(
            BinaryPrimitives.ReadInt32LittleEndian(response.AsSpan(12)),
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(16)),
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(20)) != 0,
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(24)) != 0,
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(28)),
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(32)),
            BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(36)) != 0,
            BinaryPrimitives.ReadInt32LittleEndian(response.AsSpan(40)),
            BinaryPrimitives.ReadUInt64LittleEndian(response.AsSpan(48)),
            ReadUtf16(response.AsSpan(56, 128)));
        if (status.Result < 0 && command != AgentCommand.GetStatus)
            throw new COMException(
                "El agente rechazó la operación.", status.Result);
        return status;
    }

    private static string ReadUtf16(ReadOnlySpan<byte> value)
    {
        var terminator = 0;
        while (terminator + 1 < value.Length &&
               (value[terminator] != 0 || value[terminator + 1] != 0))
            terminator += 2;
        return System.Text.Encoding.Unicode.GetString(value[..terminator]);
    }

    private static void ValidateServer(SafePipeHandle pipe)
    {
        if (!GetNamedPipeServerProcessId(pipe, out var processId) ||
            processId == 0)
            throw new UnauthorizedAccessException(
                "No se pudo identificar el agente.");
        using var process = Process.GetProcessById(checked((int)processId));
        var actual = Path.GetFullPath(
            process.MainModule?.FileName ??
            throw new UnauthorizedAccessException(
                "No se pudo validar el ejecutable del agente."));
        var expected = Path.GetFullPath(
            Path.Combine(AppContext.BaseDirectory, "DfgtAgent.exe"));
        if (!actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
            throw new UnauthorizedAccessException(
                "El pipe no pertenece al agente instalado esperado.");
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeServerProcessId(
        SafePipeHandle pipe,
        out uint serverProcessId);
}
