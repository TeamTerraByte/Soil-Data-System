using System;
using System.ComponentModel.DataAnnotations;

namespace ThingSpeakDebugger.Models
{
    public class ThingSpeakQueryViewModel
    {
        [Required]
        public int? ChannelId { get; set; }
        public string? ApiKey { get; set; }
        public int? Results { get; set; } = 100;
        public DateTimeOffset? Start { get; set; }
        public DateTimeOffset? End { get; set; }
        public string? Timezone { get; set; }
        public ThingSpeakResponse? Response { get; set; }
        public string? Error { get; set; }
    }
}
