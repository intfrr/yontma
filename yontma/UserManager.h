
#define YONTMA_SERVICE_ACCOUNT_NAME L"yontmauser"
#define YONTMA_SERVICE_ACCOUNT_NAME_WITH_DOMAIN L".\\ " YONTMA_SERVICE_ACCOUNT_NAME

HRESULT CreateServiceUserAccount(__out PWSTR* ppszAccountPassword, __out size_t* cbPassword);
HRESULT RemoveServiceUserAccount(void);
BOOL CreateYontmaUser(WCHAR *wcPassword,DWORD dwPwdSize);
BOOL DeleteYontmaUser(void);