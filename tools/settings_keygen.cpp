#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct FieldInfo
{
    std::string enum_name;
    std::string name_en;
    std::string property_name;
    std::string value_type;
};

struct GroupInfo
{
    std::string            source_file;
    std::string            yaml_key;
    std::string            enum_name;
    std::string            table_name;
    std::string            accessor;
    std::string            parent_accessor;
    std::string            accessor_path;
    std::vector<FieldInfo> fields;
};

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error(message);
}

std::string usage()
{
    return "usage: settings_keygen --input-dir <config/settings> --output <SettingsKeys.hpp>";
}

std::string toGenericPath(const std::filesystem::path &path)
{
    return path.generic_string();
}

std::string scalar(const YAML::Node &node, const char *key, const std::string &context, bool required = true)
{
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar())
    {
        if (required)
        {
            fail(context + ": missing scalar '" + key + "'");
        }
        return {};
    }
    return value.as<std::string>();
}

std::string stripSuffix(std::string value, std::string_view suffix)
{
    if (value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        value.resize(value.size() - suffix.size());
    }
    return value;
}

std::string toPascalIdentifier(const std::string &value, const std::string &context)
{
    std::string result;
    bool        start_word = true;

    for (const unsigned char ch : value)
    {
        if (std::isalnum(ch) == 0)
        {
            start_word = true;
            continue;
        }

        if (start_word)
        {
            result.push_back(static_cast<char>(std::toupper(ch)));
            start_word = false;
        }
        else
        {
            result.push_back(static_cast<char>(ch));
        }
    }

    if (result.empty())
    {
        fail(context + ": cannot derive C++ identifier from '" + value + "'");
    }
    if (std::isalpha(static_cast<unsigned char>(result.front())) == 0 && result.front() != '_')
    {
        result.insert(0, "Key");
    }

    const bool has_alpha
        = std::any_of(result.begin(), result.end(), [](const unsigned char ch) { return std::isalpha(ch) != 0; });
    const bool all_upper = has_alpha
                        && std::all_of(result.begin(), result.end(), [](const unsigned char ch)
                                       { return std::isalpha(ch) == 0 || std::isupper(ch) != 0; });
    if (all_upper)
    {
        std::transform(std::next(result.begin()), result.end(), std::next(result.begin()),
                       [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    }
    return result;
}

std::string cppStringLiteral(const std::string &value)
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                constexpr char kHex[] = "0123456789ABCDEF";
                result += "\\x";
                result.push_back(kHex[(ch >> 4) & 0x0F]);
                result.push_back(kHex[ch & 0x0F]);
            }
            else
            {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

std::vector<std::filesystem::path> yamlFiles(const std::filesystem::path &input_dir)
{
    if (!std::filesystem::is_directory(input_dir))
    {
        fail("input directory does not exist: " + toGenericPath(input_dir));
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(input_dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::string extension = entry.path().extension().string();
        if (extension == ".yaml" || extension == ".yml")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

GroupInfo parseGroup(const std::filesystem::path &file, const std::string &yaml_key, const YAML::Node &node)
{
    const std::string file_name = toGenericPath(file);
    const std::string context   = file_name + ": group " + yaml_key;

    GroupInfo         group;
    const std::string derived_group_key = stripSuffix(stripSuffix(yaml_key, "Settings"), "Setting");
    group.source_file                   = file_name;
    group.yaml_key                      = yaml_key;
    group.enum_name       = node["key"] ? toPascalIdentifier(node["key"].as<std::string>(), context + ".key")
                                        : toPascalIdentifier(derived_group_key, context);
    group.table_name      = scalar(node, "table", context);
    group.accessor        = scalar(node, "accessor", context);
    group.parent_accessor = scalar(node, "parent_accessor", context, false);
    group.accessor_path = group.parent_accessor.empty() ? group.accessor : group.parent_accessor + "." + group.accessor;
    const YAML::Node sections = node["sections"];
    if (!sections || !sections.IsMap())
    {
        fail(context + ": missing sections map");
    }

    std::set<std::string> field_enum_names;
    std::set<std::string> field_names;
    std::set<std::string> property_names;

    for (YAML::const_iterator section_it = sections.begin(); section_it != sections.end(); ++section_it)
    {
        if (!section_it->first.IsScalar())
        {
            fail(context + ": section key must be a scalar");
        }

        const std::string section_name    = section_it->first.as<std::string>();
        const YAML::Node  section_fields  = section_it->second;
        const std::string section_context = context + ".sections[" + section_name + "]";
        if (!section_fields || !section_fields.IsSequence())
        {
            fail(section_context + ": expected field sequence");
        }

        for (std::size_t i = 0; i < section_fields.size(); ++i)
        {
            const YAML::Node field_node = section_fields[i];
            if (!field_node || !field_node.IsMap())
            {
                fail(section_context + ": field #" + std::to_string(i) + " is not a map");
            }

            const std::string field_context = section_context + "[" + std::to_string(i) + "]";
            FieldInfo         field;
            field.name_en       = scalar(field_node, "name_en", field_context);
            field.property_name = scalar(field_node, "property_name", field_context);
            field.value_type    = scalar(field_node, "value_type", field_context);
            field.enum_name     = field_node["key"]
                                    ? toPascalIdentifier(field_node["key"].as<std::string>(), field_context + ".key")
                                    : toPascalIdentifier(field.name_en, field_context + ".name_en");

            if (!field_enum_names.insert(field.enum_name).second)
            {
                fail(field_context + ": duplicate field key '" + field.enum_name + "'");
            }
            if (!field_names.insert(field.name_en).second)
            {
                fail(field_context + ": duplicate name_en '" + field.name_en + "'");
            }
            if (!property_names.insert(field.property_name).second)
            {
                fail(field_context + ": duplicate property_name '" + field.property_name + "'");
            }

            group.fields.push_back(std::move(field));
        }
    }

    return group;
}

std::vector<GroupInfo> parseGroups(const std::filesystem::path &input_dir)
{
    std::vector<GroupInfo> groups;
    std::set<std::string>  group_enum_names;
    std::set<std::string>  accessor_paths;

    for (const std::filesystem::path &file : yamlFiles(input_dir))
    {
        YAML::Node root = YAML::LoadFile(file.string());
        if (!root || !root.IsMap())
        {
            fail(toGenericPath(file) + ": root must be a map");
        }

        for (YAML::const_iterator it = root.begin(); it != root.end(); ++it)
        {
            const std::string yaml_key = it->first.as<std::string>();
            GroupInfo         group    = parseGroup(file, yaml_key, it->second);
            if (!group_enum_names.insert(group.enum_name).second)
            {
                fail(toGenericPath(file) + ": duplicate group key '" + group.enum_name + "'");
            }
            if (!accessor_paths.insert(group.accessor_path).second)
            {
                fail(toGenericPath(file) + ": duplicate accessor path '" + group.accessor_path + "'");
            }
            groups.push_back(std::move(group));
        }
    }

    std::sort(groups.begin(), groups.end(),
              [](const GroupInfo &lhs, const GroupInfo &rhs) { return lhs.enum_name < rhs.enum_name; });
    return groups;
}

void writeSwitch(std::ostream &out, const std::string &function_name, const std::string &enum_type,
                 const std::vector<std::pair<std::string, std::string>> &cases)
{
    out << "inline constexpr std::string_view " << function_name << "(" << enum_type << " key) noexcept\n";
    out << "{\n";
    out << "    switch (key)\n";
    out << "    {\n";
    for (const auto &[case_name, value] : cases)
    {
        out << "    case " << enum_type << "::" << case_name << ":\n";
        out << "        return " << cppStringLiteral(value) << ";\n";
    }
    out << "    }\n";
    out << "    return {};\n";
    out << "}\n\n";
}

std::string generateHeader(const std::vector<GroupInfo> &groups)
{
    std::ostringstream out;
    out << "#pragma once\n\n";
    out << "// Generated by tools/settings_keygen.cpp. Do not edit.\n\n";
    out << "#include \"dltool/settings/Export.h\"\n\n";
    out << "#include <QtQml>\n";
    out << "#include <string_view>\n\n";
    out << "namespace dltool::settings::generated {\n\n";

    out << "Q_NAMESPACE_EXPORT(SETTINGS_API)\n\n";
    out << "enum class AccessorKey\n";
    out << "{\n";
    for (const GroupInfo &group : groups)
    {
        out << "    " << group.enum_name << ",\n";
    }
    out << "};\n\n";
    out << "Q_ENUM_NS(AccessorKey)\n";
    out << "QML_NAMED_ELEMENT(SettingsAccessor)\n\n";
    for (const GroupInfo &group : groups)
    {
        out << "inline constexpr AccessorKey " << group.enum_name << " = AccessorKey::" << group.enum_name << ";\n";
    }
    out << '\n';

    std::vector<std::pair<std::string, std::string>> group_key_cases;
    std::vector<std::pair<std::string, std::string>> table_name_cases;
    std::vector<std::pair<std::string, std::string>> accessor_path_cases;
    for (const GroupInfo &group : groups)
    {
        group_key_cases.emplace_back(group.enum_name, group.yaml_key);
        table_name_cases.emplace_back(group.enum_name, group.table_name);
        accessor_path_cases.emplace_back(group.enum_name, group.accessor_path);
    }
    writeSwitch(out, "groupKey", "AccessorKey", group_key_cases);
    writeSwitch(out, "tableName", "AccessorKey", table_name_cases);
    writeSwitch(out, "accessorPath", "AccessorKey", accessor_path_cases);

    out << "namespace field {\n\n";
    for (const GroupInfo &group : groups)
    {
        out << "namespace " << group.enum_name << " {\n";
        out << "Q_NAMESPACE_EXPORT(SETTINGS_API)\n\n";
        out << "enum class Key\n";
        out << "{\n";
        for (const FieldInfo &field : group.fields)
        {
            out << "    " << field.enum_name << ",\n";
        }
        out << "};\n\n";
        out << "Q_ENUM_NS(Key)\n";
        out << "QML_NAMED_ELEMENT(" << group.enum_name << "Field)\n\n";
        for (const FieldInfo &field : group.fields)
        {
            out << "inline constexpr Key " << field.enum_name << " = Key::" << field.enum_name << ";\n";
        }
        out << "\n} // namespace " << group.enum_name << "\n\n";
    }
    out << "} // namespace field\n\n";

    for (const GroupInfo &group : groups)
    {
        const std::string enum_type = "field::" + group.enum_name + "::Key";
        out << "inline constexpr AccessorKey accessorFor(" << enum_type << ") noexcept\n";
        out << "{\n";
        out << "    return AccessorKey::" << group.enum_name << ";\n";
        out << "}\n\n";

        std::vector<std::pair<std::string, std::string>> field_name_cases;
        std::vector<std::pair<std::string, std::string>> property_name_cases;
        std::vector<std::pair<std::string, std::string>> value_type_cases;
        for (const FieldInfo &field : group.fields)
        {
            field_name_cases.emplace_back(field.enum_name, field.name_en);
            property_name_cases.emplace_back(field.enum_name, field.property_name);
            value_type_cases.emplace_back(field.enum_name, field.value_type);
        }
        writeSwitch(out, "fieldName", enum_type, field_name_cases);
        writeSwitch(out, "propertyName", enum_type, property_name_cases);
        writeSwitch(out, "valueType", enum_type, value_type_cases);
    }

    out << "inline constexpr std::string_view fieldName(AccessorKey accessor_key, int field_key) noexcept\n";
    out << "{\n";
    out << "    switch (accessor_key)\n";
    out << "    {\n";
    for (const GroupInfo &group : groups)
    {
        out << "    case AccessorKey::" << group.enum_name << ":\n";
        out << "        return fieldName(static_cast<field::" << group.enum_name << "::Key>(field_key));\n";
    }
    out << "    }\n";
    out << "    return {};\n";
    out << "}\n\n";

    out << "inline constexpr std::string_view propertyName(AccessorKey accessor_key, int field_key) noexcept\n";
    out << "{\n";
    out << "    switch (accessor_key)\n";
    out << "    {\n";
    for (const GroupInfo &group : groups)
    {
        out << "    case AccessorKey::" << group.enum_name << ":\n";
        out << "        return propertyName(static_cast<field::" << group.enum_name << "::Key>(field_key));\n";
    }
    out << "    }\n";
    out << "    return {};\n";
    out << "}\n\n";

    out << "inline constexpr std::string_view valueType(AccessorKey accessor_key, int field_key) noexcept\n";
    out << "{\n";
    out << "    switch (accessor_key)\n";
    out << "    {\n";
    for (const GroupInfo &group : groups)
    {
        out << "    case AccessorKey::" << group.enum_name << ":\n";
        out << "        return valueType(static_cast<field::" << group.enum_name << "::Key>(field_key));\n";
    }
    out << "    }\n";
    out << "    return {};\n";
    out << "}\n\n";

    out << "} // namespace dltool::settings::generated\n";
    return out.str();
}

void writeIfChanged(const std::filesystem::path &output, const std::string &content)
{
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec)
    {
        fail("failed to create output directory: " + output.parent_path().generic_string() + ": " + ec.message());
    }

    {
        std::ifstream existing(output, std::ios::binary);
        if (existing)
        {
            std::ostringstream buffer;
            buffer << existing.rdbuf();
            if (buffer.str() == content)
            {
                return;
            }
        }
    }

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail("failed to open output file: " + toGenericPath(output));
    }
    out << content;
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::filesystem::path input_dir;
        std::filesystem::path output;

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "--input-dir" && i + 1 < argc)
            {
                input_dir = argv[++i];
            }
            else if (arg == "--output" && i + 1 < argc)
            {
                output = argv[++i];
            }
            else
            {
                fail(usage());
            }
        }

        if (input_dir.empty() || output.empty())
        {
            fail(usage());
        }

        const std::vector<GroupInfo> groups = parseGroups(input_dir);
        if (groups.empty())
        {
            fail("no settings groups found in: " + toGenericPath(input_dir));
        }

        writeIfChanged(output, generateHeader(groups));
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "settings_keygen: " << e.what() << '\n';
        return 1;
    }
}
