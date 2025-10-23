using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using ThingSpeakDebugger.Services;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddControllersWithViews();
builder.Services.AddHttpClient<IThingSpeakClient, ThingSpeakClient>();
var app = builder.Build();
app.UseStaticFiles();
app.MapControllerRoute("default", "{controller=ThingSpeak}/{action=Index}/{id?}");
app.Run();
