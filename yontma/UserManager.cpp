
#include "stdafx.h"

#define YONTMA_SERVICE_ACCOUNT_COMMENT L"Service account for YoNTMA (You'll Never Take Me Alive!) service."
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

BOOL AdjustYontmaAccountPrivileges(void);
bool InitLsaString(PLSA_UNICODE_STRING pLsaString,LPCWSTR pwszString);
BOOL GenerateRandomPassword(WCHAR *pBuffer,DWORD dwSize);

//
// Description:
//  Creates a new user account under which the YoNTMA service will run.
//
// Parameters:
//  ppszAccountPassword - On success, contains the randomly generated password of the new account.
//      Caller must free with HB_SECURE_FREE.
//
//  cbAccountPassword - On success, is set to the size (in bytes) of ppszAccountPassword.
//
HRESULT CreateServiceUserAccount(__out PWSTR* ppszAccountPassword, __out size_t* cbAccountPassword)
{
    HRESULT hr;
    PWSTR pszAccountPasswordLocal;
    DWORD badParameterIndex;

    //
    // This value is chosen arbitrarily as a long password. Could increase or decrease if there are
    // compatibility issues.
    //

    const size_t cchPasswordLength = 40;

    size_t cbAccountPasswordLocal;
    hr = SizeTMult(cchPasswordLength, sizeof(WCHAR), &cbAccountPasswordLocal);
    if(HB_FAILED(hr)) {
        goto cleanexit;
    }

    pszAccountPasswordLocal = (PWSTR)malloc(cbAccountPasswordLocal);
    if(!pszAccountPasswordLocal) {
        hr = E_OUTOFMEMORY;
        goto cleanexit;
    }

    //TODO: Replace with a secure random password
    hr = StringCbCopy(pszAccountPasswordLocal, cbAccountPasswordLocal, L"fakefornow");
    if(HB_FAILED(hr)) {
        goto cleanexit;
    }

    USER_INFO_1 userInfo = {
      YONTMA_SERVICE_ACCOUNT_NAME,
      pszAccountPasswordLocal,
      0,
      USER_PRIV_USER,
      NULL,
      YONTMA_SERVICE_ACCOUNT_COMMENT,
      UF_DONT_EXPIRE_PASSWD,
      NULL
    };
    
    if(NetUserAdd(NULL,
                  1,
                  (LPBYTE)&userInfo,
                  &badParameterIndex) != NERR_Success) {
        hr = E_FAIL;
        goto cleanexit;
    }

    *ppszAccountPassword = pszAccountPasswordLocal;
    pszAccountPasswordLocal = NULL;

    *cbAccountPassword = cbAccountPasswordLocal;

    hr = S_OK;

cleanexit:
    HB_SECURE_FREE(pszAccountPasswordLocal, cbAccountPasswordLocal);

    return hr;
}

HRESULT RemoveServiceUserAccount(void)
{
    HRESULT hr;

    if(NetUserDel(NULL, YONTMA_SERVICE_ACCOUNT_NAME) != NERR_Success) {
        hr = E_FAIL;
        goto cleanexit;
    }

    hr = S_OK;

cleanexit:

    return hr;
}

//Creates a user for the yontma service
//returns TRUE if user is created, FALSE otherwise
BOOL CreateYontmaUser(WCHAR *wcPassword,DWORD dwPwdSize)
{
	USER_INFO_0 *pUserInfo0;
	USER_INFO_1 UserInfo1 = {0};
	DWORD dwResult,dwReturn,dwEntries,dwTotalEntries,i;
	GROUP_USERS_INFO_0 *pGroupInfo0;
	LOCALGROUP_MEMBERS_INFO_3 LocalGroupMembersInfo3 = {0};

	//Check if the user already exist. Delete if it does
	dwResult = NetUserGetInfo(NULL,YONTMA_SERVICE_ACCOUNT_NAME,0,(LPBYTE*)&pUserInfo0);
	NetApiBufferFree(pUserInfo0);
	if(dwResult == NERR_Success)
	{
		if(NetUserDel(NULL,YONTMA_SERVICE_ACCOUNT_NAME) != NERR_Success) return FALSE;
	}

	//generate a new, random passwords
	if(!GenerateRandomPassword(wcPassword,dwPwdSize)) return FALSE;

	//create a new user
	UserInfo1.usri1_name = YONTMA_SERVICE_ACCOUNT_NAME;
	UserInfo1.usri1_password = wcPassword;
	UserInfo1.usri1_priv = USER_PRIV_USER;
	dwResult = NetUserAdd(NULL,1,(LPBYTE)&UserInfo1,NULL);
	if(dwResult != NERR_Success) {
		return FALSE;
	}

	//we need this to succeed to be able to use our new user
	if(!AdjustYontmaAccountPrivileges()) return FALSE;
	else return TRUE;

	//remove user fom all groups
	//we return true even if this fails since this user will still be more low-priv than SYSTEM
	if(NetUserGetGroups(NULL,YONTMA_SERVICE_ACCOUNT_NAME,0,(LPBYTE*)&pGroupInfo0,MAX_PREFERRED_LENGTH,&dwEntries,&dwTotalEntries) == NERR_Success) {
		for(i = 0; i < dwEntries; i++) {
			LocalGroupMembersInfo3.lgrmi3_domainandname = YONTMA_SERVICE_ACCOUNT_NAME;
			NetLocalGroupDelMembers(NULL,pGroupInfo0[i].grui0_name,3,(LPBYTE)&LocalGroupMembersInfo3,1);
		}
		NetApiBufferFree(pGroupInfo0);
	}
		
	return TRUE;
}

BOOL AdjustYontmaAccountPrivileges(void)
{
	PBYTE SidBuffer[128];
	PSID pSid = (PSID)SidBuffer;
	DWORD dwSid = sizeof(SidBuffer);
	WCHAR wcRefDomain[128];
	DWORD dwRefDomain = sizeof(wcRefDomain) / sizeof(WCHAR);
	SID_NAME_USE SidNameUse;
	LSA_OBJECT_ATTRIBUTES ObjectAttributes = {0};
	LSA_HANDLE lsahPolicyHandle;
	LSA_UNICODE_STRING lucStr;
	
	if(!LookupAccountName(NULL,YONTMA_SERVICE_ACCOUNT_NAME,&SidBuffer,&dwSid,wcRefDomain,&dwRefDomain,&SidNameUse)) {
		return FALSE;
	}

	if(LsaOpenPolicy(NULL,&ObjectAttributes,POLICY_ALL_ACCESS,&lsahPolicyHandle) != STATUS_SUCCESS) return FALSE;

	InitLsaString(&lucStr,SE_SERVICE_LOGON_NAME);
	if(LsaAddAccountRights(lsahPolicyHandle,pSid,&lucStr,1) != STATUS_SUCCESS) {
		LsaClose(lsahPolicyHandle);
		return FALSE;
	}

	//when we remove privs, we don't care if we fail
	InitLsaString(&lucStr,SE_BATCH_LOGON_NAME);
	LsaRemoveAccountRights(lsahPolicyHandle,pSid,FALSE,&lucStr,1);
	InitLsaString(&lucStr,SE_INTERACTIVE_LOGON_NAME);
	LsaRemoveAccountRights(lsahPolicyHandle,pSid,FALSE,&lucStr,1);
	InitLsaString(&lucStr,SE_NETWORK_LOGON_NAME);
	LsaRemoveAccountRights(lsahPolicyHandle,pSid,FALSE,&lucStr,1);
	InitLsaString(&lucStr,SE_REMOTE_INTERACTIVE_LOGON_NAME);
	LsaRemoveAccountRights(lsahPolicyHandle,pSid,FALSE,&lucStr,1);

	LsaClose(lsahPolicyHandle);
	return TRUE;
}

bool InitLsaString(PLSA_UNICODE_STRING pLsaString,LPCWSTR pwszString)
{
	DWORD dwLen = 0;

	if (NULL == pLsaString) return FALSE;

	if (NULL != pwszString) {
		dwLen = wcslen(pwszString);
		if (dwLen > 0x7ffe) return FALSE;
	}

	// Store the string.
	pLsaString->Buffer = (WCHAR *)pwszString;
	pLsaString->Length =  (USHORT)dwLen * sizeof(WCHAR);
	pLsaString->MaximumLength= (USHORT)(dwLen+1) * sizeof(WCHAR);

	return TRUE;
}

//dwSize is the size of pBuffer in WCHAR
BOOL GenerateRandomPassword(WCHAR *pBuffer,DWORD dwSize)
{
	HCRYPTPROV hCryptProvider;
	BYTE bRandomBuffer[256];
	DWORD i;

	if(dwSize > sizeof(bRandomBuffer) * sizeof(WCHAR)) dwSize = sizeof(bRandomBuffer) * sizeof(WCHAR);
	
	if(!CryptAcquireContext(&hCryptProvider,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)) {
		printf("CryptAcquireContext error: 0x%08x\n",GetLastError());
		return FALSE;
	}

	if(!CryptGenRandom(hCryptProvider,sizeof(bRandomBuffer),bRandomBuffer)) {
		CryptReleaseContext(hCryptProvider,0);
		return FALSE;
	}

	//Make sure all byte are within printable range. We do lose some entropy, but we still have enough
	//Also convert into WCHAR
	for(i = 0; i < dwSize - 1; i++) pBuffer[i] = (bRandomBuffer[i] % 0x4d) + 0x21;
	pBuffer[i] = '\0';

	CryptReleaseContext(hCryptProvider,0);
	return TRUE;
}
