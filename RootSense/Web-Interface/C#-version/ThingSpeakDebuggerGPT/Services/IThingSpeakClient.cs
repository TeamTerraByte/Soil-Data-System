using System;
using System.Threading.Tasks;
using ThingSpeakDebugger.Models;

namespace ThingSpeakDebugger.Services
{
    public interface IThingSpeakClient
    {
        Task<ThingSpeakResponse> GetChannelFeedsAsync(int channelId, string? apiKey, int? results,
            DateTimeOffset? start, DateTimeOffset? end, string? timezone);
    }
}
