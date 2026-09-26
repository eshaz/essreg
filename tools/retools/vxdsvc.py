# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows 9x VxD device IDs, service names and control messages.

The tables cover the services referenced by the ESS drivers.  Names come from
the Windows 3.1/95 DDK service tables (vmm.inc, vpicd.inc, vdmad.inc,
shell.inc, vxdldr.inc, configmg.h, mmdevldr.h).  Services whose number could
not be matched with certainty are left unnamed; ``service_name`` then returns
a generic ``<DEV>_Service_<nnnn>`` name.
"""

DEVICES = {
    0x0001: "VMM",
    0x0003: "VPICD",
    0x0004: "VDMAD",
    0x0005: "VTD",
    0x0017: "SHELL",
    0x0027: "VXDLDR",
    0x002A: "VWIN32",
    0x0033: "CONFIGMG",
    0x044A: "MMDEVLDR",
    0x357E: "DSOUND",
    0x3B07: "ES1869",
    0x3738: "ES1868",
}

_VMM = """
Get_VMM_Version Get_Cur_VM_Handle Test_Cur_VM_Handle Get_Sys_VM_Handle
Test_Sys_VM_Handle Validate_VM_Handle Get_VMM_Reenter_Count
Begin_Reentrant_Execution End_Reentrant_Execution Install_V86_Break_Point
Remove_V86_Break_Point Allocate_V86_Call_Back Allocate_PM_Call_Back
Call_When_VM_Returns Schedule_Global_Event Schedule_VM_Event
Call_Global_Event Call_VM_Event Cancel_Global_Event Cancel_VM_Event
Call_Priority_VM_Event Cancel_Priority_VM_Event Get_NMI_Handler_Addr
Set_NMI_Handler_Addr Hook_NMI_Event Call_When_VM_Ints_Enabled Enable_VM_Ints
Disable_VM_Ints Map_Flat Map_Lin_To_VM_Addr Adjust_Exec_Priority
Begin_Critical_Section End_Critical_Section End_Crit_And_Suspend
Claim_Critical_Section Release_Critical_Section Call_When_Not_Critical
Create_Semaphore Destroy_Semaphore Wait_Semaphore Signal_Semaphore
Get_Crit_Section_Status Call_When_Task_Switched Suspend_VM Resume_VM
No_Fail_Resume_VM Nuke_VM Crash_Cur_VM Get_Execution_Focus
Set_Execution_Focus Get_Time_Slice_Priority Set_Time_Slice_Priority
Get_Time_Slice_Granularity Set_Time_Slice_Granularity Get_Time_Slice_Info
Adjust_Execution_Time Release_Time_Slice Wake_Up_VM Call_When_Idle
Get_Next_VM_Handle Set_Global_Time_Out Set_VM_Time_Out Cancel_Time_Out
Get_System_Time Get_VM_Exec_Time Hook_V86_Int_Chain Get_V86_Int_Vector
Set_V86_Int_Vector Get_PM_Int_Vector Set_PM_Int_Vector Simulate_Int
Simulate_Iret Simulate_Far_Call Simulate_Far_Jmp Simulate_Far_Ret
Simulate_Far_Ret_N Build_Int_Stack_Frame Simulate_Push Simulate_Pop
_HeapAllocate _HeapReAllocate _HeapFree _HeapGetSize _PageAllocate
_PageReAllocate _PageFree _PageLock _PageUnLock _PageGetSizeAddr
_PageGetAllocInfo _GetFreePageCount _GetSysPageCount _GetVMPgCount
_MapIntoV86 _PhysIntoV86 _TestGlobalV86Mem _ModifyPageBits _CopyPageTable
_LinMapIntoV86 _LinPageLock _LinPageUnLock _SetResetV86Pageable
_GetV86PageableArray _PageCheckLinRange _PageOutDirtyPages
_PageDiscardPages _GetNulPageHandle _GetFirstV86Page _MapPhysToLinear
_GetAppFlatDSAlias _SelectorMapFlat _GetDemandPageInfo _GetSetPageOutCount
Hook_V86_Page _Assign_Device_V86_Pages _DeAssign_Device_V86_Pages
_Get_Device_V86_Pages_Array MMGR_SetNULPageAddr _Allocate_GDT_Selector
_Free_GDT_Selector _Allocate_LDT_Selector _Free_LDT_Selector
_BuildDescriptorDWORDs _GetDescriptor _SetDescriptor _MMGR_Toggle_HMA
Get_Fault_Hook_Addrs Hook_V86_Fault Hook_PM_Fault Hook_VMM_Fault
Begin_Nest_V86_Exec Begin_Nest_Exec Exec_Int Resume_Exec End_Nest_Exec
Allocate_PM_App_CB_Area Get_Cur_PM_App_CB Set_V86_Exec_Mode
Set_PM_Exec_Mode Begin_Use_Locked_PM_Stack End_Use_Locked_PM_Stack
Save_Client_State Restore_Client_State Exec_VxD_Int Hook_Device_Service
Hook_Device_V86_API Hook_Device_PM_API System_Control Simulate_IO
Install_Mult_IO_Handlers Install_IO_Handler Enable_Global_Trapping
Enable_Local_Trapping Disable_Global_Trapping Disable_Local_Trapping
List_Create List_Destroy List_Allocate List_Attach List_Attach_Tail
List_Insert List_Remove List_Deallocate List_Get_First List_Get_Next
List_Remove_First _AddInstanceItem _Allocate_Device_CB_Area
_Allocate_Global_V86_Data_Area _Allocate_Temp_V86_Data_Area
_Free_Temp_V86_Data_Area Get_Profile_Decimal_Int Convert_Decimal_String
Get_Profile_Fixed_Point Convert_Fixed_Point_String Get_Profile_Hex_Int
Convert_Hex_String Get_Profile_Boolean Convert_Boolean_String
Get_Profile_String Get_Next_Profile_String Get_Environment_String
Get_Exec_Path Get_Config_Directory OpenFile Get_PSP_Segment GetDOSVectors
Get_Machine_Info GetSet_HMA_Info Set_System_Exit_Code Fatal_Error_Handler
Fatal_Memory_Error Update_System_Clock Test_Debug_Installed Out_Debug_String
Out_Debug_Chr In_Debug_Chr Debug_Convert_Hex_Binary
Debug_Convert_Hex_Decimal Debug_Test_Valid_Handle Validate_Client_Ptr
Test_Reenter Queue_Debug_String Log_Proc_Call Debug_Test_Cur_VM
Get_PM_Int_Type Set_PM_Int_Type Get_Last_Updated_System_Time
Get_Last_Updated_VM_Exec_Time Test_DBCS_Lead_Byte _AddFreePhysPage
_PageResetHandlePAddr _SetLastV86Page _GetLastV86Page _MapFreePhysReg
_UnmapFreePhysReg _XchgFreePhysReg _SetFreePhysRegCalBk Get_Next_Arena
Get_Name_Of_Ugly_TSR Get_Debug_Options Set_Physical_HMA_Alias
_GetGlblRng0V86IntBase _Add_Global_V86_Data_Area GetSetDetailedVMError
Is_Debug_Chr Clear_Mono_Screen Out_Mono_Chr Set_Mono_Cur_Pos
Get_Mono_Cur_Pos Get_Mono_Chr Locate_Byte_In_ROM Hook_Invalid_Page_Fault
Unhook_Invalid_Page_Fault Set_Delete_On_Exit_File Close_VM
Enable_Touch_1st_Meg Disable_Touch_1st_Meg Install_Exception_Handler
Remove_Exception_Handler Get_Crit_Status_No_Block
""".split()

_VPICD = """
Get_Version Virtualize_IRQ Set_Int_Request Clear_Int_Request Phys_EOI
Get_Complete_Status Get_Status Test_Phys_Request Physically_Mask
Physically_Unmask Set_Auto_Masking Get_IRQ_Complete_Status
Convert_Handle_To_IRQ Convert_IRQ_To_Int Convert_Int_To_IRQ
Call_When_Hw_Int Force_Default_Owner Force_Default_Behavior
Auto_Mask_At_Inst_Swap Begin_Inst_Page_Swap End_Inst_Page_Swap Virtual_EOI
Get_Virtualization_Count Post_Sys_Critical_Init VM_SlavePIC_Mask_Change
""".split()

_VDMAD = """
Get_Version Virtualize_Channel Get_Region_Info Set_Region_Info
Get_Virt_State Set_Virt_State Set_Phys_State Mask_Channel UnMask_Channel
Lock_DMA_Region Unlock_DMA_Region Scatter_Lock Scatter_Unlock
Reserve_Buffer_Space Request_Buffer Release_Buffer Copy_To_Buffer
Copy_From_Buffer Default_Handler Disable_Translation Enable_Translation
Get_EISA_Adr_Mode Set_EISA_Adr_Mode Unlock_DMA_Region_No_Dirty
Phys_Mask_Channel Phys_Unmask_Channel Unvirtualize_Channel Set_IO_Address
Get_Phys_Count Get_Phys_Status Get_Max_Phys_Page Set_Channel_Callbacks
Get_Virt_Count Set_Virt_Count
""".split()

_SHELL = """
Get_Version Resolve_Contention Event SYSMODAL_Message Message GetVMInfo
_PostMessage _ShellExecute _PostShellMessage DispatchRing0AppyEvents
Hook_Properties Unhook_Properties Update_User_Activity
_QueryAppyTimeAvailable _CallAtAppyTime _CancelAppyTimeEvent
""".split()

_VXDLDR = """
Get_Version LoadDevice UnloadDevice DevInitSucceeded DevInitFailed
GetDeviceList UnloadMe
""".split()

_CONFIGMG = """
Get_Version Initialize Locate_DevNode Get_Parent Get_Child Get_Sibling
Get_Device_ID_Size Get_Device_ID Get_Depth Get_Private_DWord
Set_Private_DWord Create_DevNode Query_Remove_SubTree Remove_SubTree
Register_Device_Driver Register_Enumerator Register_Arbitrator
Deregister_Arbitrator Query_Arbitrator_Free_Size Query_Arbitrator_Free_Data
Sort_NodeList Yield Lock Unlock Add_Empty_Log_Conf Free_Log_Conf
Get_First_Log_Conf Get_Next_Log_Conf Add_Res_Des Modify_Res_Des
Free_Res_Des Get_Next_Res_Des Get_Performance_Info Get_Res_Des_Data_Size
Get_Res_Des_Data Process_Events_Now Create_Range_List Add_Range
Delete_Range Test_Range_Available Dup_Range_List Free_Range_List
Invert_Range_List Intersect_Range_List First_Range Next_Range
Dump_Range_List Load_DLVxDs Get_DDBs Get_CRC_CheckSum Register_DevLoader
Reenumerate_DevNode Setup_DevNode Reset_Children_Marks Get_DevNode_Status
Remove_Unmarked_Children ISAPNP_To_CM CallBack_Device_Driver
CallBack_Enumerator Get_Alloc_Log_Conf Get_DevNode_Key_Size
Get_DevNode_Key Read_Registry_Value Write_Registry_Value
""".split()

_MMDEVLDR = """
Register_Device_Driver SetDevicePresence SetEnvironmentString
GetEnvironmentString RemoveEnvironmentString AddEnvironmentString
""".split()

_DSOUND = """
Get_Version RegisterDeviceDriver DeregisterDeviceDriver
""".split()

SERVICES = {
    0x0001: _VMM, 0x0003: _VPICD, 0x0004: _VDMAD, 0x0017: _SHELL,
    0x0027: _VXDLDR, 0x0033: _CONFIGMG, 0x044A: _MMDEVLDR, 0x357E: _DSOUND,
}

CONTROL_MESSAGES = [
    "Sys_Critical_Init", "Device_Init", "Init_Complete", "Sys_VM_Init",
    "Sys_VM_Terminate", "System_Exit", "Sys_Critical_Exit", "Create_VM",
    "VM_Critical_Init", "VM_Init", "VM_Terminate", "VM_Not_Executeable",
    "Destroy_VM", "VM_Suspend", "VM_Resume", "Set_Device_Focus",
    "Begin_Message_Mode", "End_Message_Mode", "Reboot_Processor",
    "Query_Destroy", "Debug_Query", "Begin_PM_App", "End_PM_App",
    "Device_Reboot_Notify", "Crit_Reboot_Notify", "Close_VM_Notify",
    "Power_Event", "Sys_Dynamic_Device_Init", "Sys_Dynamic_Device_Exit",
    "Create_Thread", "Thread_Init", "Terminate_Thread",
    "Thread_Not_Executeable", "Destroy_Thread", "PnP_New_DevNode",
    "W32_DeviceIoControl",
]


def device_name(dev):
    return DEVICES.get(dev, "DEV_%04X" % dev)


def service_name(dev, svc):
    """Symbolic name of VxD service ``svc`` of device ``dev`` (no jmp bit)."""
    table = SERVICES.get(dev)
    if table and svc < len(table):
        name = table[svc]
        if dev == 0x0001:
            return name
        return "%s_%s" % (device_name(dev), name.lstrip("_"))
    return "%s_Service_%04X" % (device_name(dev), svc)
