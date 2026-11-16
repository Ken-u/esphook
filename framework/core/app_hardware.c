#include "app_hardware.h"
#include <string.h>
const char *app_hardware_profile_name(app_hardware_profile_t profile)
{
    const char *names[]={"auto","screen","robot"};
    return (unsigned)profile<3?names[profile]:"unknown";
}
bool app_hardware_profile_parse(const char *name,app_hardware_profile_t *profile)
{
    if(!name || !profile) return false;
    for(unsigned i=0;i<3;++i) if(!strcmp(name,app_hardware_profile_name((app_hardware_profile_t)i))) {
        *profile=(app_hardware_profile_t)i;return true;
    }
    return false;
}
app_hardware_policy_t app_hardware_policy(app_hardware_profile_t profile)
{
    if(profile==APP_PROFILE_ROBOT) return (app_hardware_policy_t){.radar=true,.servo=true,.physical_keys=1};
    if(profile==APP_PROFILE_AUTO) return (app_hardware_policy_t){.radar=true,.servo=false,.physical_keys=3};
    return (app_hardware_policy_t){.radar=false,.servo=false,.physical_keys=3};
}
