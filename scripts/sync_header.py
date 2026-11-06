with open('scripts/web_voice_studio.html', 'r', encoding='utf-8') as f:
    html = f.read()

header = '#pragma once\n#include <pgmspace.h>\n\nconst char INDEX_HTML[] PROGMEM = R"rawliteral(\n' + html + '\n)rawliteral";\n'

with open('firmware/roboarm_phone_offline_voice/web_studio.h', 'w', encoding='utf-8') as f:
    f.write(header)

print("web_studio.h synced successfully!")
