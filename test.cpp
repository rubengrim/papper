#include <iostream>
#include <ranges>
#include <string_view>

consteval bool validate_align_spec(std::string_view align)
{
    char positioning = align[0];
    if (positioning != '<' && positioning != '>' && positioning != '^')
    {
        return false;
    }

    std::string_view width = align.substr(1);
    if (width.size() == 0)
    {
        return false;
    }

    for (char c : width)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }

    return true;
}

int main()
{
    static_assert(validate_align_spec("<13242"));
}
