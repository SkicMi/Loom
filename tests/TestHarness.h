#pragma once
#include "Vulkan/VulkanInstance.h"
#include <cstdarg>
#include <cstdio>
#include <string>

//A check either holds or the test fails. Printing a number is not a test, so every
//measurement below ends in one of these
inline std::string fmt(const char* format, ...){
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}

class TestReport{
    public:
    explicit TestReport(const char* testName) : name(testName){
        printf("== %s\n", name.c_str());
        VulkanInstance::resetValidationMessages();
    }

    void check(const char* what, bool passed, const std::string& detail){
        printf("   %-4s %-22s %s\n", passed ? "ok" : "FAIL", what, detail.c_str());
        ++total;
        if(!passed) ++failures;
    }

    //Every test ends with this. A run that draws the right pixels while the validation
    //layers complain is not a passing run
    void checkNoValidationMessages(){
        const uint32_t count = VulkanInstance::getValidationMessageCount();
        check("validation", count == 0, fmt("%u warninga ili gresaka", count));
    }

    int result(){
        printf("== %s: %d/%d proslo\n\n", name.c_str(), total - failures, total);
        return failures == 0 ? 0 : 1;
    }

    private:
    std::string name;
    int total = 0;
    int failures = 0;
};
