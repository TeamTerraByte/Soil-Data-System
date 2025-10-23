using System;
using System.Globalization;
using System.Net.Http;
using System.Text;
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
            var sb = new StringBuilder($"https://api.thingspeak.com/channels/{channelId}/feeds.json");
            bool first = true;
            void add(string k, string v)
            {
                sb.Append(first ? '?' : '&');
                first = false;
                sb.Append(k).Append('=').Append(Uri.EscapeDataString(v));
            }

            if (!string.IsNullOrWhiteSpace(apiKey)) add("api_key", apiKey.Trim());
            if (results is > 0) add("results", results.Value.ToString(CultureInfo.InvariantCulture));
            if (start.HasValue) add("start", start.Value.UtcDateTime.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture));
            if (end.HasValue) add("end", end.Value.UtcDateTime.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture));
            if (!string.IsNullOrWhiteSpace(timezone)) add("timezone", timezone.Trim());

            var res = await _http.GetAsync(sb.ToString());
            res.EnsureSuccessStatusCode();
            await using var stream = await res.Content.ReadAsStreamAsync();
            return await JsonSerializer.DeserializeAsync<ThingSpeakResponse>(stream) ?? new ThingSpeakResponse();
        }
    }
}
