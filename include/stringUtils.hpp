#pragma once
#include <string>
#include <vector>
#include <stack>

static std::vector<std::string> splitBracketAware(const std::string &str, const std::string &delimiter = ",")
{
    std::vector<std::string> tokens;
    if (delimiter.empty())
    {
        tokens.push_back(str);
        return tokens;
    }

    size_t start = 0;
    std::stack<char> bracketStack;
    size_t i = 0;
    while (i < str.length())
    {
        char c = str[i];
        if (c == '(' || c == '[' || c == '{')
        {
            bracketStack.push(c);
            ++i;
            continue;
        }
        else if (c == ')' || c == ']' || c == '}')
        {
            if (!bracketStack.empty())
                bracketStack.pop();
            ++i;
            continue;
        }

        // if we're not inside brackets and the delimiter matches at this position
        if (bracketStack.empty() && i + delimiter.size() <= str.length() &&
            str.compare(i, delimiter.size(), delimiter) == 0)
        {
            tokens.push_back(str.substr(start, i - start));
            i += delimiter.size();
            start = i;
            continue;
        }

        ++i;
    }

    if (start < str.length())
    {
        tokens.push_back(str.substr(start));
    }

    // trim whitespace from tokens
    for (auto &t : tokens)
    {
        size_t s = 0;
        size_t e = t.size();
        while (s < e && std::isspace(static_cast<unsigned char>(t[s])))
            ++s;
        while (e > s && std::isspace(static_cast<unsigned char>(t[e - 1])))
            --e;
        t = t.substr(s, e - s);
    }

    return tokens;
}

static void splitNameConfig(const std::string &str, std::string &outName, std::string &outConfig)
{
    size_t parenPos = str.find('(');
    if (parenPos != std::string::npos)
    {
        outName = str.substr(0, parenPos);
        size_t endParenPos = str.rfind(')');
        if (endParenPos != std::string::npos && endParenPos > parenPos)
        {
            outConfig = str.substr(parenPos + 1, endParenPos - parenPos - 1);
        }
        else
        {
            outConfig = str.substr(parenPos + 1);
        }
    }
    else
    {
        outName = str;
        outConfig = "";
    }
}