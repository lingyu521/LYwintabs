namespace LYIsland.Transfer;

/// <summary>A bounded moving average over actual payload progress using a monotonic clock.</summary>
internal sealed class TransferRateEstimator(TimeProvider? timeProvider = null)
{
    private static readonly TimeSpan Window = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan MinimumSample = TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan IdleTimeout = TimeSpan.FromMilliseconds(1500);
    private readonly TimeProvider _clock = timeProvider ?? TimeProvider.System;
    private readonly List<(long Timestamp, long Bytes)> _samples = new(12);
    private long _bytes, _lastProgress, _lastSample;

    public void Reset(long bytes = 0)
    {
        _samples.Clear();
        _bytes = bytes;
        _lastProgress = _lastSample = _clock.GetTimestamp();
        _samples.Add((_lastSample, bytes));
    }

    public void Record(long bytes)
    {
        if (_samples.Count == 0 || bytes < _bytes) Reset(bytes);
        if (bytes <= _bytes) return;
        long now = _clock.GetTimestamp();
        // Resume with a fresh window, so a previous burst cannot inflate the new rate.
        if (_clock.GetElapsedTime(_lastProgress, now) >= IdleTimeout) Reset(_bytes);
        _bytes = bytes;
        _lastProgress = now;
        if (_clock.GetElapsedTime(_lastSample, now) >= MinimumSample)
        {
            _samples.Add((now, bytes));
            _lastSample = now;
        }
        Trim(now);
    }

    public double GetBytesPerSecond()
    {
        if (_samples.Count == 0) return 0;
        long now = _clock.GetTimestamp();
        if (_clock.GetElapsedTime(_lastProgress, now) >= IdleTimeout) return 0;
        Trim(now);
        var first = _samples[0];
        TimeSpan duration = _clock.GetElapsedTime(first.Timestamp, now);
        return duration < MinimumSample ? 0 : Math.Max(0, _bytes - first.Bytes) / duration.TotalSeconds;
    }

    private void Trim(long now)
    {
        // Retain the sample just before the window boundary to avoid a rate spike.
        while (_samples.Count > 1 && _clock.GetElapsedTime(_samples[1].Timestamp, now) >= Window)
            _samples.RemoveAt(0);
    }
}
