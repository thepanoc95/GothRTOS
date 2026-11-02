#include <ctype.h>

int isalpha(int character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

int isdigit(int character)
{
    return character >= '0' && character <= '9';
}

int isalnum(int character)
{
    return isalpha(character) || isdigit(character);
}

int isspace(int character)
{
    return character == ' ' || (character >= '\t' && character <= '\r');
}

int isblank(int character)
{
    return character == ' ' || character == '\t';
}

int isprint(int character)
{
    return character >= 0x20 && character <= 0x7e;
}

int isgraph(int character)
{
    return character > 0x20 && character <= 0x7e;
}

int iscntrl(int character)
{
    return (character >= 0 && character < 0x20) || character == 0x7f;
}

int ispunct(int character)
{
    return isgraph(character) && !isalnum(character);
}

int islower(int character)
{
    return character >= 'a' && character <= 'z';
}

int isupper(int character)
{
    return character >= 'A' && character <= 'Z';
}

int isxdigit(int character)
{
    return isdigit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

int tolower(int character)
{
    return isupper(character) ? character + ('a' - 'A') : character;
}

int toupper(int character)
{
    return islower(character) ? character - ('a' - 'A') : character;
}
