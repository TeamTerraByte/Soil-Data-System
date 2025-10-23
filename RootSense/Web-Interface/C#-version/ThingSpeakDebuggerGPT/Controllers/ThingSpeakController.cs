using System;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;
using ThingSpeakDebugger.Models;
using ThingSpeakDebugger.Services;

namespace ThingSpeakDebugger.Controllers
{
    public class ThingSpeakController : Controller
    {
        private readonly IThingSpeakClient _client;

        public ThingSpeakController(IThingSpeakClient client)
        {
            _client = client;
        }

        [HttpGet]
        public IActionResult Index()
        {
            return View(new ThingSpeakQueryViewModel());
        }

        [HttpPost]
        [ValidateAntiForgeryToken]
        public async Task<IActionResult> Index(ThingSpeakQueryViewModel model)
        {
            if (!ModelState.IsValid || model.ChannelId is null)
                return View(model);

            try
            {
                var data = await _client.GetChannelFeedsAsync(
                    model.ChannelId.Value,
                    model.ApiKey,
                    model.Results,
                    model.Start,
                    model.End,
                    model.Timezone);

                model.Response = data;
            }
            catch (Exception ex)
            {
                model.Error = $"Failed to query ThingSpeak: {ex.Message}";
            }

            return View(model);
        }
    }
}
