using System;
using System.ComponentModel.DataAnnotations;

namespace ThingSpeakDebugger.Models
{
    public class ThingSpeakQueryViewModel
    {
        [Required]
        [Display(Name = "Channel ID")]
        public int? ChannelId { get; set; }

        [Display(Name = "Read API Key (optional)")]
        public string? ApiKey { get; set; }

        [Range(1, 8000)]
        [Display(Name = "Results")]
        public int? Results { get; set; } = 100;

        [Display(Name = "Start (UTC)")]
        public DateTimeOffset? Start { get; set; }

        [Display(Name = "End (UTC)")]
        public DateTimeOffset? End { get; set; }

        [Display(Name = "Timezone")]
        public string? Timezone { get; set; }

        public ThingSpeakResponse? Response { get; set; }
        public string? Error { get; set; }
    }
}
