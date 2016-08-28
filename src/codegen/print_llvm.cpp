#define SSA_PRINT_TO_STDOUT 0

void ssa_fprintf(gbFile *f, char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	gb_fprintf_va(f, fmt, va);
#if SSA_PRINT_TO_STDOUT
	gb_printf_va(fmt, va);
#endif
	va_end(va);
}

void ssa_file_write(gbFile *f, void *data, isize len) {
	gb_file_write(f, data, len);
#if SSA_PRINT_TO_STDOUT
	gb_file_write(gb_file_get_standard(gbFileStandard_Output), data, len);
#endif
}

b32 ssa_valid_char(u8 c) {
	if (c >= 0x80)
		return false;

	if (gb_char_is_alphanumeric(c))
		return true;

	switch (c) {
	case '$':
	case '-':
	case '.':
	case '_':
		return true;
	}

	return false;
}

void ssa_print_escape_string(gbFile *f, String name, b32 print_quotes) {
	isize extra = 0;
	for (isize i = 0; i < name.len; i++) {
		u8 c = name.text[i];
		if (!ssa_valid_char(c))
			extra += 2;
	}

	if (extra == 0) {
		ssa_fprintf(f, "%.*s", LIT(name));
		return;
	}


	char hex_table[] = "0123456789ABCDEF";
	isize buf_len = name.len + extra + 2;
	u8 *buf = gb_alloc_array(gb_heap_allocator(), u8, buf_len);
	defer (gb_free(gb_heap_allocator(), buf));

	isize j = 0;

	if (print_quotes) {
		buf[j++] = '"';
	}

	for (isize i = 0; i < name.len; i++) {
		u8 c = name.text[i];
		if (ssa_valid_char(c)) {
			buf[j++] = c;
		} else {
			buf[j] = '\\';
			buf[j+1] = hex_table[c >> 4];
			buf[j+2] = hex_table[c & 0x0f];
			j += 3;
		}
	}

	if (print_quotes) {
		buf[j++] = '"';
	}

	ssa_file_write(f, buf, j);
}



void ssa_print_encoded_local(gbFile *f, String name) {
	ssa_fprintf(f, "%%");
	ssa_print_escape_string(f, name, true);
}

void ssa_print_encoded_global(gbFile *f, String name) {
	ssa_fprintf(f, "@");
	ssa_print_escape_string(f, name, true);
}


void ssa_print_type(gbFile *f, BaseTypeSizes s, Type *t) {
	i64 word_bits = 8*s.word_size;
	GB_ASSERT_NOT_NULL(t);
	t = default_type(t);

	switch (t->kind) {
	case Type_Basic:
		switch (t->Basic.kind) {
		case Basic_bool:   ssa_fprintf(f, "i1");                      break;
		case Basic_i8:     ssa_fprintf(f, "i8");                      break;
		case Basic_i16:    ssa_fprintf(f, "i16");                     break;
		case Basic_i32:    ssa_fprintf(f, "i32");                     break;
		case Basic_i64:    ssa_fprintf(f, "i64");                     break;
		case Basic_i128:   ssa_fprintf(f, "i128");                    break;
		case Basic_u8:     ssa_fprintf(f, "i8");                      break;
		case Basic_u16:    ssa_fprintf(f, "i16");                     break;
		case Basic_u32:    ssa_fprintf(f, "i32");                     break;
		case Basic_u64:    ssa_fprintf(f, "i64");                     break;
		case Basic_u128:   ssa_fprintf(f, "u128");                    break;
		case Basic_f32:    ssa_fprintf(f, "float");                   break;
		case Basic_f64:    ssa_fprintf(f, "double");                  break;
		case Basic_rawptr: ssa_fprintf(f, "%%.rawptr");               break;
		case Basic_string: ssa_fprintf(f, "%%.string");               break;
		case Basic_uint:   ssa_fprintf(f, "i%lld", word_bits);        break;
		case Basic_int:    ssa_fprintf(f, "i%lld", word_bits);        break;
		}
		break;
	case Type_Array:
		ssa_fprintf(f, "[%lld x ", t->Array.count);
		ssa_print_type(f, s, t->Array.elem);
		ssa_fprintf(f, "]");
		break;
	case Type_Vector: {
		// TODO(bill): actually do correctly
		ssa_fprintf(f, "<%lld x ", t->Vector.count);
		ssa_print_type(f, s, t->Vector.elem);
		ssa_fprintf(f, ">");
	} break;
	case Type_Slice:
		ssa_fprintf(f, "{");
		ssa_print_type(f, s, t->Slice.elem);
		ssa_fprintf(f, "*, i%lld, i%lld}", word_bits, word_bits);
		break;
	case Type_Struct:
		if (t->Struct.is_packed) {
			ssa_fprintf(f, "<");
		}
		ssa_fprintf(f, "{");
		for (isize i = 0; i < t->Struct.field_count; i++) {
			if (i > 0) {
				ssa_fprintf(f, ", ");
			}
			Type *ft = t->Struct.fields[i]->type;
			Type *bft = get_base_type(ft);
			if (bft->kind != Type_Struct) {
				ft = bft;
			}
			ssa_print_type(f, s, ft);
		}
		ssa_fprintf(f, "}");
		if (t->Struct.is_packed) {
			ssa_fprintf(f, ">");
		}

		break;
	case Type_Union: {
		i64 size = type_size_of(s, gb_heap_allocator(), t);
		ssa_fprintf(f, "[%lld x i8]", size);
	} break;
	case Type_Enum:
		ssa_print_type(f, s, t->Enum.base);
		break;
	case Type_Pointer:
		ssa_print_type(f, s, t->Pointer.elem);
		ssa_fprintf(f, "*");
		break;
	case Type_Named:
		ssa_print_encoded_local(f, t->Named.name);
		break;
	case Type_Tuple:
		if (t->Tuple.variable_count == 1) {
			ssa_print_type(f, s, t->Tuple.variables[0]->type);
		} else {
			ssa_fprintf(f, "{");
			for (isize i = 0; i < t->Tuple.variable_count; i++) {
				if (i > 0) ssa_fprintf(f, ", ");
				ssa_print_type(f, s, t->Tuple.variables[i]->type);
			}
			ssa_fprintf(f, "}");
		}
		break;
	case Type_Proc: {
		if (t->Proc.result_count == 0) {
			ssa_fprintf(f, "void");
		} else {
			ssa_print_type(f, s, t->Proc.results);
		}
		ssa_fprintf(f, " (");
		auto *params = &t->Proc.params->Tuple;
		for (isize i = 0; i < t->Proc.param_count; i++) {
			if (i > 0) {
				ssa_fprintf(f, ", ");
			}
			ssa_print_type(f, s, params->variables[i]->type);
		}
		ssa_fprintf(f, ")*");
	} break;
	}
}

void ssa_print_exact_value(gbFile *f, ssaModule *m, ExactValue value, Type *type) {
	type = get_base_type(type);
	if (is_type_float(type)) {
		value = exact_value_to_float(value);
	} else if (is_type_integer(type)) {
		value = exact_value_to_integer(value);
	} else if (is_type_pointer(type)) {
		value = exact_value_to_integer(value);
	}

	switch (value.kind) {
	case ExactValue_Bool:
		ssa_fprintf(f, (value.value_bool ? "true" : "false"));
		break;
	case ExactValue_String: {
		ssa_fprintf(f, "c\"");
		ssa_print_escape_string(f, value.value_string, false);
		ssa_fprintf(f, "\"");
	} break;
	case ExactValue_Integer: {
		if (is_type_pointer(type)) {
			if (value.value_integer == 0) {
				ssa_fprintf(f, "null");
			} else {
				ssa_fprintf(f, "inttoptr (");
				ssa_print_type(f, m->sizes, t_int);
				ssa_fprintf(f, " %llu to ", value.value_integer);
				ssa_print_type(f, m->sizes, t_rawptr);
				ssa_fprintf(f, ")");
			}
		} else {
			ssa_fprintf(f, "%lld", value.value_integer);
		}
	} break;
	case ExactValue_Float: {
		u64 u = *cast(u64*)&value.value_float;
		if (is_type_float(type) && type->Basic.kind == Basic_f32) {
			// IMPORTANT NOTE(bill): LLVM requires all floating point constants to be
			// a 64 bit number if bits_of(float type) <= 64.
			// For some bizarre reason, you need to clear the bottom 29 bits
			// https://groups.google.com/forum/#!topic/llvm-dev/IlqV3TbSk6M
			u >>= 29;
			u <<= 29;
		}
		ssa_fprintf(f, "0x%016llx", u);
	} break;
	case ExactValue_Pointer:
		if (value.value_pointer == NULL) {
			ssa_fprintf(f, "null");
		} else {
			ssa_fprintf(f, "inttoptr (");
			ssa_print_type(f, m->sizes, t_int);
			ssa_fprintf(f, " %llu to ", cast(u64)cast(uintptr)value.value_pointer);
			ssa_print_type(f, m->sizes, t_rawptr);
			ssa_fprintf(f, ")");
		}
		break;
	default:
		GB_PANIC("Invalid ExactValue: %d", value.kind);
		break;
	}
}

void ssa_print_block_name(gbFile *f, ssaBlock *b) {
	ssa_print_escape_string(f, b->label, false);
	ssa_fprintf(f, ".-.%d", b->id);
}

void ssa_print_value(gbFile *f, ssaModule *m, ssaValue *value, Type *type_hint) {
	if (value == NULL) {
		ssa_fprintf(f, "!!!NULL_VALUE");
		return;
	}
	switch (value->kind) {
	case ssaValue_Constant:
		ssa_print_exact_value(f, m, value->Constant.value, type_hint);
		break;
	case ssaValue_TypeName:
		ssa_print_encoded_local(f, value->TypeName.name);
		break;
	case ssaValue_Global:
		ssa_print_encoded_global(f, value->Global.entity->token.string);
		break;
	case ssaValue_Param:
		ssa_print_encoded_local(f, value->Param.entity->token.string);
		break;
	case ssaValue_Proc:
		ssa_print_encoded_global(f, value->Proc.name);
		break;
	case ssaValue_Instr:
		ssa_fprintf(f, "%%%d", value->id);
		break;
	}
}

void ssa_print_instr(gbFile *f, ssaModule *m, ssaValue *value) {
	GB_ASSERT(value->kind == ssaValue_Instr);
	ssaInstr *instr = &value->Instr;

	ssa_fprintf(f, "\t");

	switch (instr->kind) {
	case ssaInstr_StartupRuntime: {
		ssa_fprintf(f, "call void @" SSA_STARTUP_RUNTIME_PROC_NAME "()\n");
	} break;

	case ssaInstr_Local: {
		Type *type = instr->Local.entity->type;
		ssa_fprintf(f, "%%%d = alloca ", value->id);
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, ", align %lld ", type_align_of(m->sizes, m->allocator, type));
		{
			String str = instr->Local.entity->token.string;
			if (str.len > 0)
			ssa_fprintf(f, "; %.*s", LIT(instr->Local.entity->token.string));
		}
		ssa_fprintf(f, "\n");
		ssa_fprintf(f, "\tstore ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, " zeroinitializer, ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, "* %%%d\n", value->id);
	} break;

	case ssaInstr_Store: {
		Type *type = ssa_type(instr->Store.address);
		ssa_fprintf(f, "store ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->Store.value, type);
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, "* ");
		ssa_print_value(f, m, instr->Store.address, type);
		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_Load: {
		Type *type = instr->Load.type;
		ssa_fprintf(f, "%%%d = load ", value->id);
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, "* ");
		ssa_print_value(f, m, instr->Load.address, type);
		ssa_fprintf(f, ", align %lld\n", type_align_of(m->sizes, m->allocator, type));
	} break;

	case ssaInstr_GetElementPtr: {
		Type *et = instr->GetElementPtr.elem_type;
		ssa_fprintf(f, "%%%d = getelementptr ", value->id);
		if (instr->GetElementPtr.inbounds) {
			ssa_fprintf(f, "inbounds ");
		}

		ssa_print_type(f, m->sizes, et);
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, et);
		ssa_fprintf(f, "* ");
		ssa_print_value(f, m, instr->GetElementPtr.address, et);
		for (isize i = 0; i < instr->GetElementPtr.index_count; i++) {
			ssaValue *index = instr->GetElementPtr.indices[i];
			Type *t = ssa_type(index);
			ssa_fprintf(f, ", ");
			ssa_print_type(f, m->sizes, t);
			ssa_fprintf(f, " ");
			ssa_print_value(f, m, index, t);
		}
		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_ExtractValue: {
		Type *et = instr->ExtractValue.elem_type;
		ssa_fprintf(f, "%%%d = extractvalue ", value->id);

		ssa_print_type(f, m->sizes, et);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->ExtractValue.address, et);
		ssa_fprintf(f, ", %d\n", instr->ExtractValue.index);
	} break;

	case ssaInstr_NoOp: {;
		ssa_fprintf(f, "%%%d = add i32 0, 0\n", value->id);
	} break;

	case ssaInstr_Br: {;
		ssa_fprintf(f, "br ");
		if (instr->Br.cond != NULL) {
			ssa_print_type(f, m->sizes, t_bool);
			ssa_fprintf(f, " ");
			ssa_print_value(f, m, instr->Br.cond, t_bool);
			ssa_fprintf(f, ", ", instr->Br.cond->id);
		}
		ssa_fprintf(f, "label ");
		ssa_fprintf(f, "%%"); ssa_print_block_name(f, instr->Br.true_block);
		if (instr->Br.false_block != NULL) {
			ssa_fprintf(f, ", label ");
			ssa_fprintf(f, "%%"); ssa_print_block_name(f, instr->Br.false_block);
		}
		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_Ret: {
		auto *ret = &instr->Ret;
		ssa_fprintf(f, "ret ");
		if (ret->value == NULL) {
			ssa_fprintf(f, "void");
		} else {
			Type *t = ssa_type(ret->value);
			ssa_print_type(f, m->sizes, t);
			ssa_fprintf(f, " ");
			ssa_print_value(f, m, ret->value, t);
		}

		ssa_fprintf(f, "\n");

	} break;

	case ssaInstr_Conv: {
		auto *c = &instr->Conv;
		ssa_fprintf(f, "%%%d = %.*s ", value->id, LIT(ssa_conv_strings[c->kind]));
		ssa_print_type(f, m->sizes, c->from);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, c->value, c->from);
		ssa_fprintf(f, " to ");
		ssa_print_type(f, m->sizes, c->to);
		ssa_fprintf(f, "\n");

	} break;

	case ssaInstr_Unreachable: {
		ssa_fprintf(f, "unreachable\n");
	} break;

	case ssaInstr_BinaryOp: {
		auto *bo = &value->Instr.BinaryOp;
		Type *type = get_base_type(ssa_type(bo->left));
		Type *elem_type = type;
		while (elem_type->kind == Type_Vector) {
			elem_type = get_base_type(elem_type->Vector.elem);
		}

		ssa_fprintf(f, "%%%d = ", value->id);

		if (gb_is_between(bo->op.kind, Token__ComparisonBegin+1, Token__ComparisonEnd-1)) {

			if (is_type_string(elem_type)) {
				ssa_fprintf(f, "call ");
				ssa_print_type(f, m->sizes, t_bool);
				char *runtime_proc = "";
				switch (bo->op.kind) {
				case Token_CmpEq: runtime_proc = "__string_eq"; break;
				case Token_NotEq: runtime_proc = "__string_ne"; break;
				case Token_Lt:    runtime_proc = "__string_lt"; break;
				case Token_Gt:    runtime_proc = "__string_gt"; break;
				case Token_LtEq:  runtime_proc = "__string_le"; break;
				case Token_GtEq:  runtime_proc = "__string_gt"; break;
				}

				ssa_fprintf(f, " @%s(", runtime_proc);
				ssa_print_type(f, m->sizes, type);
				ssa_fprintf(f, " ");
				ssa_print_value(f, m, bo->left, type);
				ssa_fprintf(f, ", ");
				ssa_print_type(f, m->sizes, type);
				ssa_fprintf(f, " ");
				ssa_print_value(f, m, bo->right, type);
				ssa_fprintf(f, ")\n");
				return;

			} else if (is_type_float(elem_type)) {
				ssa_fprintf(f, "fcmp ");
				switch (bo->op.kind) {
				case Token_CmpEq: ssa_fprintf(f, "oeq"); break;
				case Token_NotEq: ssa_fprintf(f, "one"); break;
				case Token_Lt:    ssa_fprintf(f, "olt"); break;
				case Token_Gt:    ssa_fprintf(f, "ogt"); break;
				case Token_LtEq:  ssa_fprintf(f, "ole"); break;
				case Token_GtEq:  ssa_fprintf(f, "oge"); break;
				}
			} else {
				ssa_fprintf(f, "icmp ");
				if (bo->op.kind != Token_CmpEq &&
				    bo->op.kind != Token_NotEq) {
					if (is_type_unsigned(elem_type)) {
						ssa_fprintf(f, "u");
					} else {
						ssa_fprintf(f, "s");
					}
				}
				switch (bo->op.kind) {
				case Token_CmpEq: ssa_fprintf(f, "eq"); break;
				case Token_NotEq: ssa_fprintf(f, "ne"); break;
				case Token_Lt:    ssa_fprintf(f, "lt"); break;
				case Token_Gt:    ssa_fprintf(f, "gt"); break;
				case Token_LtEq:  ssa_fprintf(f, "le"); break;
				case Token_GtEq:  ssa_fprintf(f, "ge"); break;
				}
			}
		} else {
			if (is_type_float(elem_type))
				ssa_fprintf(f, "f");

			switch (bo->op.kind) {
			case Token_Add:    ssa_fprintf(f, "add");  break;
			case Token_Sub:    ssa_fprintf(f, "sub");  break;
			case Token_And:    ssa_fprintf(f, "and");  break;
			case Token_Or:     ssa_fprintf(f, "or");   break;
			case Token_Xor:    ssa_fprintf(f, "xor");  break;
			case Token_Shl:    ssa_fprintf(f, "shl");  break;
			case Token_Shr:    ssa_fprintf(f, "lshr"); break;
			case Token_Mul:    ssa_fprintf(f, "mul");  break;
			case Token_Not:    ssa_fprintf(f, "xor");  break;

			case Token_AndNot: GB_PANIC("Token_AndNot Should never be called");

			default: {
				if (!is_type_float(elem_type)) {
					if (is_type_unsigned(elem_type)) ssa_fprintf(f, "u");
					else                             ssa_fprintf(f, "s");
				}

				switch (bo->op.kind) {
				case Token_Quo: ssa_fprintf(f, "div"); break;
				case Token_Mod: ssa_fprintf(f, "rem"); break;
				}
			} break;
			}
		}

		ssa_fprintf(f, " ");
		ssa_print_type(f, m->sizes, type);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, bo->left, type);
		ssa_fprintf(f, ", ");
		ssa_print_value(f, m, bo->right, type);
		ssa_fprintf(f, "\n");

	} break;

	case ssaInstr_Call: {
		auto *call = &instr->Call;
		Type *result_type = call->type;
		if (result_type) {
			ssa_fprintf(f, "%%%d = ", value->id);
		}
		ssa_fprintf(f, "call ");
		if (result_type) {
			ssa_print_type(f, m->sizes, result_type);
		} else {
			ssa_fprintf(f, "void");
		}
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, call->value, call->type);


		ssa_fprintf(f, "(");
		if (call->arg_count > 0) {
			Type *proc_type = get_base_type(ssa_type(call->value));
			GB_ASSERT(proc_type->kind == Type_Proc);
			auto *params = &proc_type->Proc.params->Tuple;
			for (isize i = 0; i < call->arg_count; i++) {
				Entity *e = params->variables[i];
				GB_ASSERT(e != NULL);
				Type *t = e->type;
				if (i > 0) {
					ssa_fprintf(f, ", ");
				}
				ssa_print_type(f, m->sizes, t);
				ssa_fprintf(f, " ");
				ssaValue *arg = call->args[i];
				ssa_print_value(f, m, arg, t);
			}
		}
		ssa_fprintf(f, ")\n");

	} break;

	case ssaInstr_Select: {
		ssa_fprintf(f, "%%%d = select i1 ", value->id);
		ssa_print_value(f, m, instr->Select.cond, t_bool);
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, ssa_type(instr->Select.true_value));
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->Select.true_value, ssa_type(instr->Select.true_value));
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, ssa_type(instr->Select.false_value));
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->Select.false_value, ssa_type(instr->Select.false_value));
		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_MemCopy: {
		ssa_fprintf(f, "call void @memory_move");
		ssa_fprintf(f, "(i8* ");
		ssa_print_value(f, m, instr->CopyMemory.dst, t_rawptr);
		ssa_fprintf(f, ", i8* ");
		ssa_print_value(f, m, instr->CopyMemory.src, t_rawptr);
		ssa_fprintf(f, ", ");
		ssa_print_type(f, m->sizes, t_int);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->CopyMemory.len, t_int);
		ssa_fprintf(f, ")\n");
	} break;


	case ssaInstr_ExtractElement: {
		Type *vt = ssa_type(instr->ExtractElement.vector);
		ssa_fprintf(f, "%%%d = extractelement ", value->id);

		ssa_print_type(f, m->sizes, vt);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->ExtractElement.vector, vt);
		ssa_fprintf(f, ", ");
		Type *it = ssa_type(instr->ExtractElement.index);
		ssa_print_type(f, m->sizes, it);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, instr->ExtractElement.index, it);
		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_InsertElement: {
		auto *ie = &instr->InsertElement;
		Type *vt = ssa_type(ie->vector);
		ssa_fprintf(f, "%%%d = insertelement ", value->id);

		ssa_print_type(f, m->sizes, vt);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, ie->vector, vt);
		ssa_fprintf(f, ", ");

		ssa_print_type(f, m->sizes, ssa_type(ie->elem));
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, ie->elem, ssa_type(ie->elem));
		ssa_fprintf(f, ", ");

		ssa_print_type(f, m->sizes, ssa_type(ie->index));
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, ie->index, ssa_type(ie->index));

		ssa_fprintf(f, "\n");
	} break;

	case ssaInstr_ShuffleVector: {
		auto *sv = &instr->ShuffleVector;
		Type *vt = ssa_type(sv->vector);
		ssa_fprintf(f, "%%%d = shufflevector ", value->id);

		ssa_print_type(f, m->sizes, vt);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, sv->vector, vt);
		ssa_fprintf(f, ", ");

		ssa_print_type(f, m->sizes, vt);
		ssa_fprintf(f, " ");
		ssa_print_value(f, m, sv->vector, vt);
		ssa_fprintf(f, ", ");

		ssa_fprintf(f, "<%td x i32> <", sv->index_count);
		for (isize i = 0; i < sv->index_count; i++) {
			if (i > 0) {
				ssa_fprintf(f, ", ");
			}
			ssa_fprintf(f, "i32 %d", sv->indices[i]);
		}
		ssa_fprintf(f, ">");
		ssa_fprintf(f, "\n");
	} break;


	default: {
		GB_PANIC("<unknown instr> %d\n", instr->kind);
		ssa_fprintf(f, "; <unknown instr> %d\n", instr->kind);
	} break;
	}
}

void ssa_print_proc(gbFile *f, ssaModule *m, ssaProcedure *proc) {
	if (proc->body == NULL) {
		ssa_fprintf(f, "\ndeclare ");
	} else {
		ssa_fprintf(f, "\ndefine ");
	}

	auto *proc_type = &proc->type->Proc;

	if (proc_type->result_count == 0) {
		ssa_fprintf(f, "void");
	} else {
		ssa_print_type(f, m->sizes, proc_type->results);
	}

	ssa_fprintf(f, " ");

	ssa_print_encoded_global(f, proc->name);
	ssa_fprintf(f, "(");

	if (proc_type->param_count > 0) {
		auto *params = &proc_type->params->Tuple;
		for (isize i = 0; i < params->variable_count; i++) {
			Entity *e = params->variables[i];
			if (i > 0)

				ssa_fprintf(f, ", ");
			ssa_print_type(f, m->sizes, e->type);
			ssa_fprintf(f, " %%%.*s", LIT(e->token.string));
		}
	}

	ssa_fprintf(f, ") ");

	if (proc->tags != 0) {
		if (proc->tags & ProcTag_inline)
			ssa_fprintf(f, "alwaysinline ");
		if (proc->tags & ProcTag_no_inline)
			ssa_fprintf(f, "noinline ");


		if (proc->tags & ProcTag_foreign)
			ssa_fprintf(f, "; foreign\n");
	}

	if (proc->body != NULL) {
		ssa_fprintf(f, "{\n");
		gb_for_array(i, proc->blocks) {
			ssaBlock *block = proc->blocks[i];

			if (i > 0) ssa_fprintf(f, "\n");
			ssa_print_block_name(f, block);
			ssa_fprintf(f, ":\n");

			gb_for_array(j, block->instrs) {
				ssaValue *value = block->instrs[j];
				ssa_print_instr(f, m, value);
			}
		}
		ssa_fprintf(f, "}\n");
	}

	gb_for_array(i, proc->children) {
		ssa_print_proc(f, m, proc->children[i]);
	}
}

void ssa_print_type_name(gbFile *f, ssaModule *m, ssaValue *v) {
	GB_ASSERT(v->kind == ssaValue_TypeName);
	ssa_print_encoded_local(f, v->TypeName.name);
	ssa_fprintf(f, " = type ");
	ssa_print_type(f, m->sizes, get_base_type(v->TypeName.type));
	ssa_fprintf(f, "\n");
}


void ssa_print_llvm_ir(gbFile *f, ssaModule *m) {
	if (m->layout.len > 0) {
		ssa_fprintf(f, "target datalayout = \"%.*s\"\n", LIT(m->layout));
	}

	ssa_print_encoded_local(f, make_string(".string"));
	ssa_fprintf(f, " = type {i8*, ");
	ssa_print_type(f, m->sizes, t_int);
	ssa_fprintf(f, "} ; Basic_string\n");

	ssa_print_encoded_local(f, make_string(".rawptr"));
	ssa_fprintf(f, " = type i8* ; Basic_rawptr\n\n");

	gb_for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		ssaValue *v = entry->value;
		switch (v->kind) {
		case ssaValue_TypeName: {
			ssa_print_encoded_local(f, v->TypeName.name);
			ssa_fprintf(f, " = type ");
			ssa_print_type(f, m->sizes, get_base_type(v->TypeName.type));
			ssa_fprintf(f, "\n");
		} break;
		}
	}

	gb_for_array(i, m->nested_type_names) {
		ssaValue *v = m->nested_type_names[i];
		ssa_print_type_name(f, m, v);
	}


	gb_for_array(member_index, m->members.entries) {
		auto *entry = &m->members.entries[member_index];
		ssaValue *v = entry->value;
		switch (v->kind) {
		case ssaValue_Global: {
			auto *g = &v->Global;
			ssa_print_encoded_global(f, g->entity->token.string);
			ssa_fprintf(f, " = ");
			if (g->is_thread_local) {
				ssa_fprintf(f, "thread_local ");
			}
			if (g->is_constant) {
				ssa_fprintf(f, "private constant ");
			} else {
				ssa_fprintf(f, "global ");
			}


			ssa_print_type(f, m->sizes, g->entity->type);
			ssa_fprintf(f, " ");
			if (g->value != NULL) {
				ssa_print_value(f, m, g->value, g->entity->type);
			} else {
				ssa_fprintf(f, "zeroinitializer");
			}
			ssa_fprintf(f, "\n");
		} break;

		case ssaValue_Proc: {
			ssa_print_proc(f, m, &v->Proc);
		} break;
		}
	}
}