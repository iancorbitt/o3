/*
 * Copyright (C) 2010 Ajax.org BV
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this library; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <npapi.h>
#include <npfunctions.h>

#ifdef O3_OSX
#include <Carbon/Carbon.h>
#endif

#ifndef O3_WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif // O3_WIN32

#include <o3.h>
#include <fs/fs.h>
#include <http/http.h>
#include <xml/xml.h>
//#include <blob/blob.h>

#ifdef O3_WITH_LIBEVENT
#include <socket/socket.h>
#endif

#ifdef O3_WIN32    
    #define O3_STDCALL __stdcall
#else
    #define O3_STDCALL
#endif



namespace o3 {

o3_iid(iScrObj, 0xf3afb7c0, 
	   0x2bf3, 
	   0x4b77, 
	   0x8e, 0x63, 0xd, 0xec, 0x25, 0xc8, 0x9e, 0x96);

struct iScrObj : iUnk {
	virtual NPObject* unwrap() = 0;
};

struct cCtx : cMgr, iCtx {
	struct O3Object : NPObject {
		NPP m_npp;
		siScr m_scr;
		
		O3Object(NPP npp, iScr* scr) : m_npp(npp), m_scr(scr)
		{
			o3_trace_hostglue("O3Object");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			
			m_scr = scr;
			m_npp = npp;
			ctx->m_objects[this] = this;            
		}
		
		~O3Object()
		{
			o3_trace_hostglue("~O3Object");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			
			if (m_scr)
				ctx->m_objects.remove(this);
		}
		
		bool hasMethod(NPIdentifier identifier)
		{			
			o3_trace_hostglue("hasMethod");			
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				int index = m_scr->resolve(ctx, name);
				
				::NPN_MemFree(name);
				if (index < 0) {
					Var proto = ctx->value(m_scr->className()) ;
					if (proto.type() == Var::TYPE_SCR) {
						NPVariant proto_var = toNPVariant(m_npp, proto);
						return proto_var.value.objectValue->_class->hasMethod(
							proto_var.value.objectValue, identifier);
					}
					return false;
				}
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_GET, index, 0, 0,
											&rval))
					return false;
				if (siScr scr = rval.toScr()) {
					//return scr->resolve(ctx, "__self__") >= 0;
					return siScrFun(scr).valid();
				}
				return false;
			} else {
				int getter = m_scr->resolve(ctx, "__getter__");
				Var arg = ::NPN_IntFromIdentifier(identifier);
				
				if (getter < 0)
					return false;
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, getter, 1,
											&arg, &rval))
					return false;
				if (siScr scr = rval.toScr())
					//return scr->resolve(ctx, "__self__") >= 0;		
					return siScrFun(scr).valid();
				return false;
			}
		}
		
		bool invoke(NPIdentifier identifier, const NPVariant* args, ::uint32_t argc,
					NPVariant* result)
		{
			o3_trace_hostglue("invoke");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			siScr scr;
			int index;
			tVec<Var> argv;
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				
				scr = m_scr;
				index = m_scr->resolve(ctx, name);
				::NPN_MemFree(name);
			} else {
				int getter = m_scr->resolve(ctx, "__getter__");
				Var arg = ::NPN_IntFromIdentifier(identifier);
				
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, getter, 1,
											&arg, &rval))
					return false;
				scr = rval.toScr();
				index = scr->resolve(ctx, "__self__");
			}
			if (index<0){
				Var proto = ctx->value(m_scr->className()) ;
				if (proto.type() == Var::TYPE_SCR) {
					NPVariant proto_var = toNPVariant(m_npp, proto);
					return proto_var.value.objectValue->_class->invoke(
						proto_var.value.objectValue, identifier, args, argc, result);
				}
			}
			for (size_t i = 0; i < argc; ++i)
				argv.push(toVar(m_npp, args[i]));
			if (siEx ex = scr->invoke(ctx, iScr::ACCESS_CALL, index, argc, argv,
									  &rval))
				return false;
			*result = toNPVariant(m_npp, rval);
			return true;
		}
		
		bool invokeDefault(const NPVariant* args, ::uint32_t argc, NPVariant* result)
		{
			o3_trace_hostglue("invokeDefault");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			int self = m_scr->resolve(ctx, "__self__");
			tVec<Var> argv;
			Var rval;
			
			if (self < 0)
				return false;
			for (size_t i = 0; i < argc; ++i)
				argv.push(toVar(m_npp, args[i]));
			if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, self, argc, argv,
										&rval))
				return false;
			*result = toNPVariant(m_npp, rval);
			return true;
		}
		
		bool hasProperty(NPIdentifier identifier)
		{
			o3_trace_hostglue("hasProperty");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				int index = m_scr->resolve(ctx, name);
				
				::NPN_MemFree(name);
				if (index < 0) {
					Var proto = ctx->value(m_scr->className()) ;
					if (proto.type() == Var::TYPE_SCR) {
						NPVariant proto_var = toNPVariant(m_npp, proto);
						return proto_var.value.objectValue->_class->hasProperty(
							proto_var.value.objectValue, identifier);
					}
					else
						index = m_scr->resolve(ctx, name, true);
				}
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_GET, index, 0, 0,
											&rval))
					return false;
				if (siScr scr = rval.toScr()) { 
					// this is a bug I have no clue why was this 
					// implemented like this I fixed the best way 
					// I could come up with... (Gabor)
					//return scr->resolve(ctx, "__self__") < 0;				
					return !siScrFun(scr).valid();
				}
			} else {
				int getter = m_scr->resolve(ctx, "__getter__");
				Var arg = ::NPN_IntFromIdentifier(identifier);
				
				if (getter < 0)
					return false;
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, getter, 1,
											&arg, &rval))
					return false;
				if (siScr scr = rval.toScr())
					return !siScrFun(scr).valid();
					//return scr->resolve(ctx, "__self__") < 0;
			}
			return true;
		}
		
		bool getProperty(NPIdentifier identifier, NPVariant* value)
		{
			o3_trace_hostglue("getProperty");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				int index = m_scr->resolve(ctx, name);
				if (index < 0) {
					Var proto = ctx->value(m_scr->className()) ;					
					if (proto.type() == Var::TYPE_SCR) {
						NPVariant proto_var = toNPVariant(m_npp, proto);
						if (strEquals(name, "prototype")){
							*value = proto_var;
							return true;
						}
						return proto_var.value.objectValue->_class->getProperty(
							proto_var.value.objectValue, identifier, value);
					}
				}
				::NPN_MemFree(name);
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_GET, index, 0, 0,
											&rval))
					return false;
			} else {
				int getter = m_scr->resolve(ctx, "__getter__");
				Var arg = ::NPN_IntFromIdentifier(identifier);
				
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, getter, 1,
											&arg, &rval))
					return false;
			}
			*value = toNPVariant(m_npp, rval);
			return true;
		}
		
		bool setProperty(NPIdentifier identifier, const NPVariant* value)
		{
			o3_trace_hostglue("setProperty");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				if (strEquals(name, "prototype")){
					Var var = toVar(m_npp, *value);
					ctx->setValue(m_scr->className(), var);
					return true;
				}
				int index = m_scr->resolve(ctx, name, true);
				Var arg = toVar(m_npp, *value);
				
				::NPN_MemFree(name);
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_SET, index, 1,
											&arg, &rval))
					return false;
			} else {
				int setter = m_scr->resolve(ctx, "__setter__");
				tVec<Var> argv;
				
				if (setter < 0)
					return false;
				argv.push(::NPN_IntFromIdentifier(identifier));
				argv.push(toVar(m_npp, *value));
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, setter, 2,
											argv, &rval))
					return false;		
			}
			return true;
		}
		
		bool removeProperty(NPIdentifier identifier)
		{
			o3_trace_hostglue("removeProperty");
			cCtx* ctx = (cCtx*) m_npp->pdata;
			Var rval(ctx);
			
			if (::NPN_IdentifierIsString(identifier)) {
				char* name = ::NPN_UTF8FromIdentifier(identifier);
				int index = m_scr->resolve(ctx, name);
				
				::NPN_MemFree(name);
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_DEL, index, 0, 0,
											&rval))
					return false;
			} else {
				int deleter = m_scr->resolve(ctx, "__deleter__");
				Var arg = ::NPN_IntFromIdentifier(identifier);
				
				if (siEx ex = m_scr->invoke(ctx, iScr::ACCESS_CALL, deleter, 1,
											&arg, &rval))
					return false;
			}
			return rval.toBool();
		}

	};
	
	struct O3Class : NPClass {
		O3Class()
		{
			o3_trace_hostglue("O3Class");
			structVersion = NP_CLASS_STRUCT_VERSION;
			allocate = &allocateFunc;
			deallocate = &deallocateFunc;
			invalidate = &invalidateFunc;
			hasMethod = &hasMethodFunc;
			invoke = &invokeFunc;
			invokeDefault = &invokeDefaultFunc;
			hasProperty = &hasPropertyFunc;
			getProperty = &getPropertyFunc;
			setProperty= &setPropertyFunc;
			removeProperty = &removePropertyFunc;
		}
		
		static NPObject* allocateFunc(NPP npp, NPClass *aClass)
		{
			o3_trace_hostglue("allocateFunc");
			cCtx* ctx = (cCtx*) npp->pdata;
			O3Object* obj = o3_new(O3Object)(npp, ctx->m_scr);
			
			obj->referenceCount = 1;
			return obj;
		}
		
		static void deallocateFunc(NPObject *object)
		{
			o3_trace_hostglue("deallocateFunc");
			o3_delete((O3Object*) object);
		}
		
		static void invalidateFunc(NPObject *object)
		{
			// TODO: Implement
		}
		
		static bool hasMethodFunc(NPObject *object, NPIdentifier identifier)
		{
			o3_trace_hostglue("hasMethodFunc");
			return ((O3Object*) object)->hasMethod(identifier);
		}
		
		static bool invokeFunc(NPObject* object, NPIdentifier identifier,
							   const NPVariant* args, ::uint32_t argc,
							   NPVariant* result)
		{
			o3_trace_hostglue("invokeFunc");
			return ((O3Object*) object)->invoke(identifier, args, argc, result);
		}
		
		static bool invokeDefaultFunc(NPObject* object, const NPVariant* args,
									  ::uint32_t argc, NPVariant* result)
		{
			o3_trace_hostglue("invokeDefaultFunc");
			return ((O3Object*) object)->invokeDefault(args, argc, result);
		}
		
		static bool hasPropertyFunc(NPObject* object, NPIdentifier identifier)
		{
			o3_trace_hostglue("hasPropertyFunc");
			return ((O3Object*) object)->hasProperty(identifier);
		}
		
		static bool getPropertyFunc(NPObject* object, NPIdentifier identifier,
									NPVariant* value)
		{
			o3_trace_hostglue("getPropertyFunc");
			return ((O3Object*) object)->getProperty(identifier, value);
		}
		
		static bool setPropertyFunc(NPObject* object, NPIdentifier identifier,
									const NPVariant* value)
		{
			o3_trace_hostglue("setPropertyFunc");
			return ((O3Object*) object)->setProperty(identifier, value);
		}
		
		static bool removePropertyFunc(NPObject* object, NPIdentifier identifier)
		{
			o3_trace_hostglue("removePropertyFunc");
			return ((O3Object*) object)->removeProperty(identifier);
		}
	};

    struct cScrObj : cScr, iScrObj {
        NPObject*   m_object;
        NPP         m_npp;
        
        cScrObj(iCtx* ctx, NPP npp, NPObject* object)
        :   m_object(object),
            m_npp(npp)
        {
            o3_trace_hostglue("cScrObj");
            NPN_RetainObject(m_object);
        }
        
        virtual ~cScrObj()
        {
            o3_trace_hostglue("~cScrObj");
            if (m_object)
                NPN_ReleaseObject(m_object);
        }

        o3_begin_class(cScr)
			o3_add_iface(iScrObj)	
        o3_end_class()
        
        int resolve(iCtx* ctx, const char* name, bool set)
        {
            o3_trace_hostglue("resolve");
            int rel_id;
            
            if (strEquals(name, "__self__"))
                return 0;
            rel_id = cScr::resolve(ctx, name, set);
            if (rel_id < 0)
                return -1;
            return rel_id + 1;
        }
        
        siEx invoke(iCtx* ctx, Access access, int rel_id, int argc,
                    const Var* argv, Var* rval)
        {
            o3_trace_hostglue("invoke");
            if (rel_id > 0) {
                cScr::invoke(ctx, access, rel_id - 1, argc, argv, rval);
                return siEx();
            }
            
            // Invoke to self
            tVec<NPVariant> np_argv(argc);
            NPVariant result;

            for (int i = 0; i < argc; ++i)
                np_argv[i] = toNPVariant(m_npp, argv[i]);
      
            if (!NPN_InvokeDefault(m_npp, m_object, np_argv.ptr(), argc,
                                   &result))
            {
                // some proper error msg would be nice here...            
                return siEx();
            }

            // Why is this ever NULL ???
            if (rval)
                *rval = toVar(m_npp, result);

            return siEx();
        }

		NPObject* unwrap() {
			return m_object;
		}
    };
 

	static Var toVar(NPP npp, const NPVariant& from)
	{
		o3_trace_hostglue("toVar");
		cCtx* ctx = (cCtx*) npp->pdata;
		NPString string;
		NPObject* object;
		
		switch (from.type) {
			case NPVariantType_Void:
				return Var(ctx);
			case NPVariantType_Null:
				return Var((iScr*) 0, ctx);
			case NPVariantType_Bool:
				return Var(NPVARIANT_TO_BOOLEAN(from), ctx);
			case NPVariantType_Int32:
				return Var(NPVARIANT_TO_INT32(from), ctx);
			case NPVariantType_Double:
				return Var(NPVARIANT_TO_DOUBLE(from), ctx);
			case NPVariantType_String:
				string = NPVARIANT_TO_STRING(from);
				return Var(Str(string.UTF8Characters, string.UTF8Length, ctx));
			case NPVariantType_Object:
				object = NPVARIANT_TO_OBJECT(from);
				
				if (object->_class == &g_class)
					return Var(((O3Object*) object)->m_scr, ctx);
				else
					return Var(o3_new(cScrObj)(ctx, npp, object)); 
		};
		return Var(ctx);
	}
	
	static NPVariant toNPVariant(NPP npp, const Var& from)
	{
		o3_trace_hostglue("toNPVariant");
		cCtx* ctx = (cCtx*) npp->pdata;
		NPVariant to;
		Str str;
		char* val;
		
		switch (from.type()) {
			case Var::TYPE_VOID:
				VOID_TO_NPVARIANT(to);
				break;
			case Var::TYPE_NULL:
				NULL_TO_NPVARIANT(to);
				break;
			case Var::TYPE_BOOL:
				BOOLEAN_TO_NPVARIANT(from.toBool(), to);
				break;
			case Var::TYPE_INT32:
				INT32_TO_NPVARIANT(from.toInt32(), to);
				break;
			case Var::TYPE_DOUBLE:
				DOUBLE_TO_NPVARIANT(from.toDouble(), to);
				break;
			case Var::TYPE_STR:
			case Var::TYPE_WSTR:
				str = from.toStr();
				val = o3::strCopy((char*) ::NPN_MemAlloc(str.size()+1), str);
				STRINGZ_TO_NPVARIANT(val, to);
				break;
			case Var::TYPE_SCR:
				siScr scr(from.toScr()); 				
				siScrObj wrapper(scr);
				if (wrapper) {
					NPObject* obj = wrapper->unwrap();
					NPN_RetainObject(obj);
					OBJECT_TO_NPVARIANT(obj, to);
					break;
				}
				ctx->m_scr = scr;
				OBJECT_TO_NPVARIANT(NPN_CreateObject(npp, &g_class), to);
				break;
		};
		return to;
	}
	
	static O3Class g_class;
	siMessageLoop m_loop;
	tMap<Str, Var> m_values;
	void* m_app_window;
	siScr m_o3;
	iScr* m_scr;
	tMap<O3Object*, O3Object*> m_objects;
	struct event_base*       m_eb;
	
	
	cCtx()
	{
o3_trace_hostglue("cCtx");
#ifdef O3_WIN32
		Str path = tmpPath();
		path.findAndReplaceAll("\\", "/");
		path.appendf("o3%s",O3_VERSION_STRING);
#else
		Str path = Str(getenv("HOME")) + "/.o3/";
		path.append(O3_VERSION_STRING);
#endif // O3_WIN32

		m_root = path;
		m_loop = g_sys->createMessageLoop();

		//addExtTraits(cBlob::extTraits());
		addExtTraits(cScrBuf::extTraits());
        addStaticExtTraits("xml", cXml::extTraits());
		addStaticExtTraits("fs", cFs::extTraits());
		
		addStaticExtTraits("socket", cSocket::extTraits());

	    addFactory("fs", &cFs::rootDir);
	    addFactory("settingsDir", &cFs::settingsDir);
		addFactory("installerDir", &cFs::installerDir);
        addFactory("pluginDir", &cFs::pluginDir);  
		addFactory("http", &cHttp::factory);	

        m_o3 = o3_new(cO3)(this, 0, 0, 0);
		m_eb =event_base_new();
	}
	
	~cCtx()
	{

		event_base_free(m_eb);
		for (tMap<O3Object*, O3Object*>::ConstIter i = m_objects.begin();
			 i != m_objects.end(); ++i)
			i->val->m_scr = 0;
		m_o3 = 0;
	}
	
	o3_begin_class(cMgr)
		o3_add_iface(iCtx)
	o3_end_class()
	
	void* alloc(size_t size)
	{
		//return ::NPN_MemAlloc(size);
		o3_trace_hostglue("alloc");
		//return ::NPN_MemAlloc(size);
		return g_sys->alloc(size);
	}
	
	void free(void* ptr)
	{
		//::NPN_MemFree(ptr);
		o3_trace_hostglue("free");
		//::NPN_MemFree(ptr);
		g_sys->free(ptr);
	}
	
	siMgr mgr()
	{
		o3_trace_hostglue("mgr");
		return this;
	}
	
    siMessageLoop loop()
	{
		o3_trace_hostglue("loop");
		return m_loop;
	}
	
    Var value(const char* key)
	{
		o3_trace_hostglue("value");
		return m_values[key];
	}
	
    Var setValue(const char* key, const Var& val)
	{
		o3_trace_hostglue("setValue");
		return m_values[key] = val;
	}
	
    Var eval(const char* name, siEx* ex = 0)
	{
		o3_trace_hostglue("eval");
		return Var(this);
	}

	virtual void setAppWindow(void* handle)
	{
		o3_trace_hostglue("setAppWindow");
		m_app_window = handle;
	}

	virtual void* appWindow() 
	{
		o3_trace_hostglue("appWindow");
		return (void*) m_app_window;
	}

	void approve(const tList<Str>& )
	{

	}

	virtual bool isIE() 
	{
		o3_trace_hostglue("isIE");
		return false;
	}

	virtual struct event_base* eventBase() 
	{
		return m_eb;
	}
};

cCtx::O3Class cCtx::g_class;

}

using namespace o3;

static NPNetscapeFuncs* g_netscape_funcs;

inline NPError NPN_GetValue(NPP instance, NPNVariable variable, void *value)
{
    o3_trace_hostglue("NPN_GetValue");
    return g_netscape_funcs->getvalue(instance, variable, value);
}

inline void* NPN_MemAlloc(::uint32_t size)
{
    o3_trace_hostglue("NPN_MemAlloc");
    return g_netscape_funcs->memalloc(size);
}

inline void NPN_MemFree(void* ptr)
{
    o3_trace_hostglue("NPN_MemFree");
    g_netscape_funcs->memfree(ptr);
}

inline NPObject* NPN_CreateObject(NPP npp, NPClass* aClass)
{
    o3_trace_hostglue("NPN_CreateObject");
    return g_netscape_funcs->createobject(npp, aClass);
}

inline void NPN_ReleaseObject(NPObject *object)
{
	o3_trace_hostglue("NPN_ReleaseObject");
	return g_netscape_funcs->releaseobject(object);
}

inline NPObject* NPN_RetainObject(NPObject* npobj) {
    o3_trace_hostglue("NPN_RetainObject");
    return g_netscape_funcs->retainobject(npobj);
}

inline bool NPN_GetProperty(NPP npp, NPObject *object, NPIdentifier identifier,
							NPVariant *value)
{
    o3_trace_hostglue("NPN_GetProperty");
    return g_netscape_funcs->getproperty(npp, object, identifier, value);
}

inline bool NPN_InvokeDefault(NPP npp, NPObject* obj, const NPVariant* args,
                              ::uint32_t argCount, NPVariant* result)
{
    o3_trace_hostglue("NPN_InvokeDefault");
    return g_netscape_funcs->invokeDefault(npp, obj, args, argCount, result);
}

inline NPIdentifier NPN_GetStringIdentifier(const NPUTF8 *name)
{
    return g_netscape_funcs->getstringidentifier(name);
}

inline bool NPN_IdentifierIsString(NPIdentifier identifier)
{
	bool string = false;
	
    if (g_netscape_funcs->identifierisstring(identifier)) {
        char* utf8 = ::NPN_UTF8FromIdentifier(identifier);
		char* str = utf8;
		
		while (*str)
            if (!o3::chrIsDigit(*str++)) {
                string = true;
				break;
            }
		::NPN_MemFree(utf8);
    }
	return string;
}

inline NPUTF8* NPN_UTF8FromIdentifier(NPIdentifier identifier)
{
    return g_netscape_funcs->utf8fromidentifier(identifier);
}

inline ::int32_t NPN_IntFromIdentifier(NPIdentifier identifier)
{
    if (g_netscape_funcs->identifierisstring(identifier)) {
        char* str = ::NPN_UTF8FromIdentifier(identifier);
        int x = o3::strToInt32(str);
        
        ::NPN_MemFree(str);
        return x;
    } else
        return g_netscape_funcs->intfromidentifier(identifier);
}

NPError NPP_New(NPMIMEType type, NPP npp, ::uint16_t mode, ::int16_t argc,
				char* argn[], char* argv[], NPSavedData* data)
{
	o3_trace_hostglue("NPP_New");
	NPObject*       object;
    NPIdentifier    identifier;
    NPVariant       value;
	NPString		string;
    o3::Str         url,full_url;
    int             index;	
	cCtx*			ctx;
	
	NPN_GetValue(npp, NPNVWindowNPObject, &object);
    identifier = NPN_GetStringIdentifier("location");
    NPN_GetProperty(npp, object, identifier, &value);
	NPN_ReleaseObject(object);
    object = NPVARIANT_TO_OBJECT(value);
    identifier = NPN_GetStringIdentifier("href");
    NPN_GetProperty(npp, object, identifier, &value);
	NPN_ReleaseObject(object);
	string = NPVARIANT_TO_STRING(value);
    full_url = url = Str(string.UTF8Characters, string.UTF8Length);
	index = url.find("://");
    url[index] = 0;
	// WTF???
	/*if (!o3::strEquals(url.ptr(), "file")) {
		hostent* he;
		in_addr addr;

		url = url.ptr() + index + 3;
        index = url.find("/");
        if (index != o3::NOT_FOUND)
            url[index] = 0;
        if (he = gethostbyname(url.ptr())) {
			o3::memCopy(&addr, he->h_addr, sizeof(in_addr));
			if (!o3::strEquals(inet_ntoa(addr), "127.0.0.1"))
				return NPERR_GENERIC_ERROR;
		}
	}*/
	ctx = o3_new(o3::cCtx)();
	ctx->addRef();
	ctx->mgr()->setCurrentUrl(full_url);
    npp->pdata = ctx;
    return NPERR_NO_ERROR;
}

NPError NPP_Destroy(NPP npp, NPSavedData** pdata)
{
    o3_trace_hostglue("NPP_Destroy");
    o3::cCtx* ctx = (o3::cCtx*) npp->pdata;
    
	ctx->release();
    return NPERR_NO_ERROR;
}

NPError NPP_SetWindow(NPP npp, NPWindow* window)
{
o3_trace_hostglue("NPP_SetWindow");
#ifdef O3_WIN32
	if (window->window) {
		HWND tab = GetParent((HWND)window->window);
		if (tab) {
			o3::cCtx* ctx = (o3::cCtx*) npp->pdata;
			ctx->setAppWindow((void*)tab);
		}
	}
#endif
	return NPERR_NO_ERROR;
}

NPError NPP_NewStream(NPP npp, NPMIMEType type, NPStream* stream,
					  NPBool seekable, ::uint16_t* stype)
{
	o3_trace_hostglue("NPP_NewStream");
	return NPERR_NO_ERROR;
}

NPError NPP_DestroyStream(NPP npp, NPStream* stream, NPReason reason)
{
	o3_trace_hostglue("NPP_DestroyStream");
	return NPERR_NO_ERROR;
}

::int32_t NPP_WriteReady(NPP npp, NPStream* stream)
{
	o3_trace_hostglue("NPP_WriteReady");
	return 0;
}

::int32_t NPP_Write(NPP instance, NPStream* stream, ::int32_t offset,
					::int32_t len, void* buffer)
{
	o3_trace_hostglue("NPP_Write");
	return 0;
}

void NPP_StreamAsFile(NPP npp, NPStream* stream, const char* fname)
{
}

void NPP_Print(NPP instance, NPPrint* print)
{
}

::int16_t NPP_HandleEvent(NPP npp, void* event)
{
o3_trace_hostglue("NPP_HandleEvent");
#ifdef O3_OSX
    EventRecord record = *(EventRecord*) event;
    
    switch (record.what) {
    
    }
#endif
	return 0;
}

void NPP_URLNotify(NPP npp, const char* url, NPReason reason, void* data)
{
}

NPError NPP_GetValue(NPP npp, NPPVariable variable, void *value)
{ 
    o3_trace_hostglue("NPP_GetValue"); 
    o3::cCtx* ctx = (o3::cCtx*) npp->pdata;
    
    switch (variable) {
		case NPPVpluginScriptableNPObject:
			ctx->m_scr = ctx->m_o3;
			*(NPObject**) value = NPN_CreateObject(npp, &cCtx::g_class);
			break;
		default:
			return NPERR_GENERIC_ERROR;
    };    
    return NPERR_NO_ERROR;
}

NPError NPP_SetValue(NPP npp, NPNVariable variable, void *value)
{
	o3_trace_hostglue("NPP_SetValue");
	return NPERR_GENERIC_ERROR;
}


extern "C" {

char* NP_GetMIMEDescription()
{
    o3_trace_hostglue("NP_GetMIMEDescription");
    return (char*)"application/basic-plugin:bsc:Basic plugin";
}

NPError O3_STDCALL NP_Initialize(NPNetscapeFuncs* funcs)
{
    o3_trace_hostglue("NP_Initialize");
    if (!g_sys)
	    g_sys = new cSys();
	
    g_netscape_funcs = funcs;
	return NPERR_NO_ERROR;
}

NPError O3_STDCALL NP_Shutdown()
{
	o3_trace_hostglue("NP_Shutdown");
	g_sys->release();
    g_sys = 0;
	g_netscape_funcs = 0;
    return NPERR_NO_ERROR;
}

NPError O3_STDCALL NP_GetEntryPoints(NPPluginFuncs* funcs)
{
	o3_trace_hostglue("NP_GetEntryPoints");
	funcs->version = 11;
	funcs->size = sizeof(funcs);
	funcs->newp = NPP_New;
	funcs->destroy = NPP_Destroy;
	funcs->setwindow = NPP_SetWindow;
	funcs->newstream = NPP_NewStream;
	funcs->destroystream = NPP_DestroyStream;
	funcs->asfile = NPP_StreamAsFile;
	funcs->writeready = NPP_WriteReady;
	funcs->write = (NPP_WriteProcPtr) NPP_Write;
	funcs->print = NPP_Print;
	funcs->event = NPP_HandleEvent;
	funcs->urlnotify = NPP_URLNotify;
	funcs->getvalue = NPP_GetValue;
	funcs->setvalue = NPP_SetValue;
	return NPERR_NO_ERROR;
}
	
}
