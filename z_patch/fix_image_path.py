import sys, io

import os
PATH = os.environ["HOME"] + "/mnt/o3de/Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp"

with io.open(PATH, "r", encoding="utf-8", newline="") as f:
    data = f.read()

eol = "\r\n" if "\r\n" in data else "\n"

def L(lines):
    return eol.join(lines)

# ---------------------------------------------------------------- edit 1
old1 = L([
'                // Images and enums need additional conversion prior to being saved.',
'                ConvertToExportFormat(templateOutputPath, materialPropertyId, *property, property->m_value);',
])

new1 = L([
'                // Captured before the conversion below, because the conversion is per file and this value is not.',
'                //',
'                // ConvertToExportFormat resolves an image reference into a path relative to the file being written, and the value',
'                // recorded further down is written into every generated material. The preview set is written two folders below the',
'                // graph, so an image sitting beside the graph converted to "../../<folder>/<image>.png", and that string, written into',
'                // the material beside the graph, walks out of the project entirely. The material builder cannot resolve it, falls back',
'                // to the invalid-asset UUID, and takes the Asset Processor down with it.',
'                //',
'                // The unconverted value is recorded instead and BuildMaterialFromTemplate converts it again for the path it is actually',
'                // writing. The conversion below still runs on the property itself, which belongs to this material type and this path.',
'                const AZ::RPI::MaterialPropertyValue materialPropertyValueBeforeConversion = property->m_value;',
'',
'                // Images and enums need additional conversion prior to being saved.',
'                ConvertToExportFormat(templateOutputPath, materialPropertyId, *property, property->m_value);',
])

# ---------------------------------------------------------------- edit 2
old2 = L([
'                const AZ::RPI::MaterialPropertyValue materialPropertyValueBeforeReset = property->m_value;',
'                if (property->m_enumValues.empty() && ResetMaterialPropertyValueToTypeDefault(property->m_value))',
'                {',
'                    if (IsViewportOutputSet())',
'                    {',
'                        m_materialPropertyValues.emplace_back(materialPropertyId, materialPropertyValueBeforeReset);',
'                    }',
'                }',
])

new2 = L([
'                if (property->m_enumValues.empty() && ResetMaterialPropertyValueToTypeDefault(property->m_value))',
'                {',
'                    if (IsViewportOutputSet())',
'                    {',
'                        m_materialPropertyValues.emplace_back(materialPropertyId, materialPropertyValueBeforeConversion);',
'                    }',
'                }',
])

# ---------------------------------------------------------------- edit 3
old3 = L([
'        for (const auto& [propertyId, propertyValue] : m_materialPropertyValues)',
'        {',
'            materialSourceData.SetPropertyValue(propertyId, propertyValue);',
'        }',
])

new3 = L([
'        for (const auto& [propertyId, propertyValue] : m_materialPropertyValues)',
'        {',
'            // The recorded values are unconverted. An image reference converts to a path relative to the file being written, and the same',
'            // recorded set is written into every generated material, so the conversion has to happen here, against the path this call is',
'            // writing, rather than once against whichever file happened to be built first.',
'            //',
'            // Only images need it. Enums are never recorded, because an enum is never reset out of the material type, and every other',
'            // recorded type converts to itself. The two image alternatives are tested directly rather than asking',
'            // GetMaterialPropertyDataTypeFromValue, which reports a plain string as an image and would send a genuine string property',
'            // through the image branch.',
'            AZ::RPI::MaterialPropertyValue convertedValue = propertyValue;',
'            if (convertedValue.Is<AZ::Data::Asset<AZ::RPI::ImageAsset>>() || convertedValue.Is<AZ::Data::Instance<AZ::RPI::Image>>())',
'            {',
'                AZ::RPI::MaterialPropertySourceData propertyDefinition;',
'                propertyDefinition.m_dataType = AZ::RPI::MaterialPropertyDataType::Image;',
'                AtomToolsFramework::ConvertToExportFormat(templateOutputPath, propertyId, propertyDefinition, convertedValue);',
'            }',
'',
'            materialSourceData.SetPropertyValue(propertyId, convertedValue);',
'        }',
])

edits = [("edit1", old1, new1), ("edit2", old2, new2), ("edit3", old3, new3)]

for name, old, new in edits:
    n = data.count(old)
    if n != 1:
        print("FAIL %s: found %d occurrences (expected 1)" % (name, n))
        sys.exit(1)

for name, old, new in edits:
    data = data.replace(old, new, 1)
    print("applied %s" % name)

with io.open(PATH, "w", encoding="utf-8", newline="") as f:
    f.write(data)

print("wrote %s (eol=%s)" % (PATH, repr(eol)))
