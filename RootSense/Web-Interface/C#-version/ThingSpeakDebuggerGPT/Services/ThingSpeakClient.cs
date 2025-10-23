using System;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using ThingSpeakDebugger.Models;

namespace ThingSpeakDebugger.Services
{
    public class ThingSpeakClient : IThingSpeakClient
    {
        private readonly HttpClient _http;

        public ThingSpeakClient(HttpClient http) => _http = http;

        public async Task<ThingSpeakResponse> GetChannelFeedsAsync(int channelId, string? apiKey, int? results,
            DateTimeOffset? start, DateTimeOffset? end, string? timezone)
        {
            var url = $"https://api.thingspeak.com/channels/{channelId}/feeds.json";
            var res = await _http.GetAsync(url);
            res.EnsureSuccessStatusCode();
            var stream = await res.Content.ReadAsStreamAsync();
            return await JsonSerializer.DeserializeAsync<ThingSpeakResponse>(stream) ?? new ThingSpeakResponse();
        }
    }
}
